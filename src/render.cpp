#include "render.h"
#include "sphere.h"
#include <vector>
#include <iostream>
#include <string>
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>



const char* vShaderSource = R"glsl(
    #version 330 core
    layout (location = 0) in vec3 aPos;
        
    uniform mat4 model;
    uniform mat4 view;
    uniform mat4 projection;

        
    void main()
    {

    gl_Position = projection * view * model * vec4(aPos, 1.0);
    }
    )glsl";
    
    
    //Fragment Shader source code
    const char* fShaderSource = R"glsl(
    #version 330 core
    out vec4 FragColor;
        
    uniform vec4 inputColor;
        
    void main()
    {
        FragColor = inputColor;
    }
    )glsl";
    

GLuint Render::compileShaderProg(){
    //unisgned integer shader object v shader is the name
    GLuint vShader = glCreateShader(GL_VERTEX_SHADER);
    //we must attach the shader 1 means number of strings in the array
    //vshdadersource points to the source code definded above
    //Null means the source string is null at the end ???
    glShaderSource(vShader, 1 , &vShaderSource, NULL);
    //compile shader
    glCompileShader(vShader);
    //exact same steps from above but here
    GLuint fShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fShader, 1 , &fShaderSource, NULL);
    glCompileShader(fShader);
    //generating a shader program to link everything
    GLuint shaderApp = glCreateProgram();
    //attaching vshader and fshader to the app
    glAttachShader(shaderApp, vShader);
    glAttachShader(shaderApp, fShader);
    //linking the program????
    glLinkProgram(shaderApp);
    //deleting shaders after we are done with them because they are already linked to the program
    glDeleteShader(vShader);
    glDeleteShader(fShader);

    return shaderApp;
}
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}
Render::Render(): VAO(0), VBO(0), shaderApp(0), sphereVertexCount(0), floorVertexCount(0){}

Render::~Render(){
    //if (VAO != 0)
    //    glDeleteVertexArrays(1, &VAO);
    //if (VBO != 0)
    //    glDeleteBuffers(1, &VBO);
    //if (shaderApp != 0)
    //    glDeleteProgram(shaderApp);
}
GLFWwindow* Render::glfwSetup(int width, int height, const char* title){
    glfwInit();
    //version 4.3, the first version with compute shaders
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    // CORE GLFW profile means we have mot recent functions only
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    //error handling for window

    GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
    //if this computer can't do 4.3, fall back to 3.3 (everything but compute shaders still works)
    if (window == NULL) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        window = glfwCreateWindow(width, height, title, NULL, NULL);
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return nullptr;
    }

    //Glad error handling
    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return nullptr;
    }

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, width, height);
    return window;
}

void Render::imguiSetup(GLFWwindow* window){
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& imio = ImGui::GetIO(); (void) imio;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Render::imguiCleanup(){
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Render::init(std::vector<GLfloat>& sphereVerticies, GLfloat* floorV, int floorV_size, int sphereVerticies_size){
    shaderApp = compileShaderProg(); 
    //floorVertexCount = sizeof(floorV)/sizeof(GLfloat)

    // Combine both into one buffer
    floorVertexCount = floorV_size;
    sphereVertexCount = sphereVerticies_size;


    std::vector<GLfloat> combinedVerticies = sphereVerticies;
    combinedVerticies.insert(combinedVerticies.end(), floorV, floorV + floorV_size);

    // Create VAO and VBO
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    //glGenBuffers(1, &instanceVBO)

    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, combinedVerticies.size() * sizeof(GLfloat), combinedVerticies.data(), GL_STATIC_DRAW);
    
    glBindVertexArray(VAO);
    // Position attribute (layout location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}  


GLuint Render::getShaderApp() {
    return shaderApp;
}

void Render::renderFloor(){
    glUseProgram(shaderApp);
    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(glGetUniformLocation(shaderApp, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniform4f(glGetUniformLocation(shaderApp, "inputColor"), 0.5f, 0.5f, 0.4f, 1.0f);

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, sphereVertexCount/3, floorVertexCount/3);
    glBindVertexArray(0);
}
void Render::renderSpheres(std::vector<Sphere>& spheres){
    glUseProgram(shaderApp);
    glBindVertexArray(VAO);

    for (Sphere& s : spheres) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), s.spherePos);
        glUniformMatrix4fv(glGetUniformLocation(shaderApp, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform4f(glGetUniformLocation(shaderApp, "inputColor"), 0.1f, 0.9f, 0.1f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, sphereVertexCount/3);
        //glDrawArraysInstanced(GL_TRIANGLES, 0, sphereVertexCount / 3, spheres.size());
    }
    glBindVertexArray(0);
}

void Render::cleanUp(){

    if (VAO != 0)
        glDeleteVertexArrays(1, &VAO);
    if (VBO != 0)
        glDeleteBuffers(1, &VBO);
    if (shaderApp != 0)
        glDeleteProgram(shaderApp);
}
