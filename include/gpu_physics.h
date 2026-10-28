#ifndef GPU_PHYSICS_H
#define GPU_PHYSICS_H
#include <glad/gl.h>

// Sphere-vs-sphere collision on the GPU using an OpenGL compute shader.
// Needs an OpenGL 4.3+ context (most Windows/Linux GPUs: Intel, AMD, NVIDIA).

// Call once after the OpenGL context exists, e.g. gpuInit(glfwGetProcAddress).
// Returns false if the GPU/driver doesn't support compute shaders.
bool gpuInit(GLADloadfunc loader);

// data: n spheres packed as 4 floats each -> x, y, z, radius
// The x, y, z values are overwritten with the collision-corrected positions.
void gpuCollide(float* data, int n);

// Frees the GPU buffers and shader. Call before closing the window.
void gpuCleanup();

#endif
