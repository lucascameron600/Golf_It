
#ifndef SHADER_H
#define SHADER_H
#include "sphere.h"
#include <vector>    
#include <string>   
#include <glad/gl.h>      
#include <glm/glm.hpp>  
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>

GLuint compileShaderProg();

class Render{
    public:
    Render();
    ~Render();


    GLFWwindow* glfwSetup(int width, int height, const char* title);
    void imguiSetup(GLFWwindow* window);
    void imguiCleanup();
    void init(std::vector<GLfloat>& sphereVerticies, GLfloat* floorV,int floorV_size, int sphereVerticies_size);
    GLuint compileShaderProg();
    void renderFloor();
    void renderSpheres(std::vector<Sphere>& spheres);
    GLuint getShaderApp();
    void cleanUp();    


    private:
        GLuint VAO, VBO;
        GLuint shaderApp;
        int sphereVertexCount;
        int floorVertexCount;
        

};


#endif
