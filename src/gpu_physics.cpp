#include "gpu_physics.h"
#include <iostream>
#include <string>

// The glad loader in this project was generated for OpenGL 3.3, which doesn't
// include compute shaders (added in 4.3). Instead of regenerating glad, we add
// the few constants and the two functions we need ourselves.
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif

typedef void (GLAD_API_PTR *DispatchComputeFn)(GLuint x, GLuint y, GLuint z);
typedef void (GLAD_API_PTR *MemoryBarrierFn)(GLbitfield barriers);
static DispatchComputeFn myDispatchCompute = nullptr;
static MemoryBarrierFn   myMemoryBarrier   = nullptr;

// One GPU thread per sphere.
// The CPU version loops over pairs (i, j) and moves both spheres at once.
// That can't run in parallel safely, because two threads could write to the
// same sphere at the same time. Instead, each thread only moves ITS OWN sphere:
// it reads everyone's old position from inSpheres, adds up all the pushes it
// gets, and writes the result to outSpheres. No two threads write to the same
// place, so no locks or atomics are needed.
// The first line of the shader depends on the OpenGL version (see gpuInit).
static const char* header430 = "#version 430\n";
static const char* header410 =
    "#version 410\n"
    "#extension GL_ARB_compute_shader : require\n"
    "#extension GL_ARB_shader_storage_buffer_object : require\n";

static const char* collideShaderSource = R"glsl(
layout(local_size_x = 256) in;

// xyz = position, w = radius
layout(std430, binding = 0) readonly  buffer InBuf  { vec4 inSpheres[];  };
layout(std430, binding = 1) writeonly buffer OutBuf { vec4 outSpheres[]; };

uniform int count;

void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(count)) return;

    vec4 me = inSpheres[i];
    vec3 push = vec3(0.0);   // total push on sphere i

    for (uint j = 0u; j < uint(count); ++j) {
        if (j == i) continue;
        vec4 other = inSpheres[j];

        vec3 delta = other.xyz - me.xyz;
        float dist2 = dot(delta, delta);
        float minDist = me.w + other.w;

        // Compare squared distances first, so sqrt only runs for real overlaps.
        if (dist2 < minDist * minDist && dist2 > 0.0) {
            float dist = sqrt(dist2);
            // Same math as Engine::checkCollision: each sphere moves half the overlap.
            push -= delta * ((minDist - dist) / dist * 0.5);
        }
    }

    outSpheres[i] = vec4(me.xyz + push, me.w);
}
)glsl";

static GLuint program = 0;
static GLuint inBuffer = 0, outBuffer = 0;
static GLint  countLocation = -1;
static int    capacity = 0;   // how many spheres the buffers can hold

bool gpuInit(GLADloadfunc loader)
{
    // Compute shaders are built into OpenGL 4.3+. Older drivers (for example
    // WSL's D3D12 driver, which reports 4.1) can still offer them as
    // extensions, so check for those too.
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    const char* header = header430;
    if (major < 4 || (major == 4 && minor < 3)) {
        bool hasCompute = false, hasStorage = false;
        GLint numExt = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExt);
        for (GLint e = 0; e < numExt; ++e) {
            std::string name = (const char*)glGetStringi(GL_EXTENSIONS, e);
            if (name == "GL_ARB_compute_shader") hasCompute = true;
            if (name == "GL_ARB_shader_storage_buffer_object") hasStorage = true;
        }
        if (!hasCompute || !hasStorage) {
            std::cout << "Compute shaders need OpenGL 4.3 (or the ARB_compute_shader and "
                         "ARB_shader_storage_buffer_object extensions), this context is "
                      << major << "." << minor << std::endl;
            return false;
        }
        header = header410;
        std::cout << "Using compute shaders through OpenGL " << major << "." << minor
                  << " extensions" << std::endl;
    }

    myDispatchCompute = (DispatchComputeFn)loader("glDispatchCompute");
    myMemoryBarrier   = (MemoryBarrierFn)loader("glMemoryBarrier");
    if (!myDispatchCompute || !myMemoryBarrier) {
        std::cout << "Could not load compute shader functions" << std::endl;
        return false;
    }

    // Compile the compute shader, same steps as the vertex/fragment shaders.
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* sources[2] = { header, collideShaderSource };
    glShaderSource(shader, 2, sources, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        std::cout << "Compute shader failed to compile:\n" << log << std::endl;
        glDeleteShader(shader);
        return false;
    }
    program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::cout << "Compute shader failed to link" << std::endl;
        return false;
    }
    countLocation = glGetUniformLocation(program, "count");

    glGenBuffers(1, &inBuffer);
    glGenBuffers(1, &outBuffer);
    return true;
}

void gpuCollide(float* data, int n)
{
    if (n < 2 || program == 0) return;
    GLsizeiptr bytes = (GLsizeiptr)n * 4 * sizeof(float);

    // Grow the GPU buffers if needed (doubling, so this rarely happens).
    if (n > capacity) {
        capacity = n * 2;
        GLsizeiptr capBytes = (GLsizeiptr)capacity * 4 * sizeof(float);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, inBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, capBytes, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, outBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, capBytes, NULL, GL_DYNAMIC_READ);
    }

    // 1. CPU -> GPU
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, inBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, data);

    // 2. Run one thread per sphere, in groups of 256 (local_size_x in the shader)
    glUseProgram(program);
    glUniform1i(countLocation, n);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, inBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outBuffer);
    myDispatchCompute((n + 255) / 256, 1, 1);

    // make sure the shader's writes are finished before we read the buffer
    myMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    // 3. GPU -> CPU (this waits for the GPU to finish)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, outBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void gpuCleanup()
{
    if (inBuffer)  glDeleteBuffers(1, &inBuffer);
    if (outBuffer) glDeleteBuffers(1, &outBuffer);
    if (program)   glDeleteProgram(program);
    inBuffer = outBuffer = program = 0;
    capacity = 0;
}
