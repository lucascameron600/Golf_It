#ifndef SPHERE_H
#define SPHERE_H
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Sphere

{
    //set initial sphere postition, 5 above y cord
    glm::vec3 spherePos = glm::vec3(0.0f,5.0f,0.0f);
    //needed for verlet integration
    glm::vec3 prevPos = spherePos;
    // Acceleration vector (resets each frame)
    glm::vec3 acceleration = glm::vec3(0.0f, -9.8f, 0.0f);
    //no more need to track velocity due to verlet
    float radius = 1.0f; 
    glm::vec4 sphereColor = glm::vec4(1.0f,0.0f,0.0f,1.0f);


};


void renderSpheres(GLuint shaderProgram, std::vector<Sphere>& spheres, int vertexCount);
std::vector<GLfloat> parseOBJ();

#endif