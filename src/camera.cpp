#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "camera.h"


Camera::Camera(float screenWidth, float screenHeight)
    : cameraPos(0.0f, 5.0f, 15.0f),
      cameraFront(0.0f, 0.0f, -1.0f),
      cameraUp(0.0f, 1.0f, 0.0f),
      speed(10.0f),
      fov(90.0f),
      nearPlane(0.1f),
      farPlane(100.0f),
      aspectRatio(screenWidth / screenHeight) {}

glm::mat4 Camera::getViewMatrix() {
    return glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
}

glm::mat4 Camera::getProjectionMatrix() {
    return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

void Camera::processInput(GLFWwindow* window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += speed * cameraFront * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= speed * cameraFront * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * speed * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * speed * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        cameraPos += cameraUp * speed * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        cameraPos -= cameraUp * speed * deltaTime;
}
