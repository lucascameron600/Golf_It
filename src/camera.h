#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    glm::vec3 cameraPos;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;

    float speed;
    float fov;
    float nearPlane;
    float farPlane;
    float aspectRatio;

    Camera(float screenWidth, float screenHeight);

    glm::mat4 getViewMatrix();
    glm::mat4 getProjectionMatrix();
    void processInput(GLFWwindow* window, float deltaTime);
};