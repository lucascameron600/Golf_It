#include <iostream>
#include <glad/gl.h>
#include <string>
#include <GLFW/glfw3.h>
#include "engine.h"
#include "sphere.h"
#include "render.h"
#include "camera.h"
#include "parseobj.h"
#include "gpu_physics.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

Render Render;
Engine Engine;

GLfloat floorV[] = {
    
    -50.0f, 0.0f, -50.0f,
     50.0f, 0.0f, -50.0f,
     50.0f, 0.0f,  50.0f,

    -50.0f, 0.0f, -50.0f,
     50.0f, 0.0f,  50.0f,
    -50.0f, 0.0f,  50.0f
};



float screenWidth = 800;
float screenHeight = 800;
Camera Camera(screenWidth, screenHeight);
//float aspectRatio  = (screenWidth/screenHeight);
//this helps us get depth perception. nearplane is the closest we will render verticies and farplane
//is the farthest
//float nearPlane    = 0.1f;
//float farPlane     = 100.0f;
//handle time caluclate fps
float deltaTime = 0.0f;
float lastFrame = 0.0f;
//used constant time in physics

//handles excape key and wasd
bool bKeyPressed = false;
float physicsAccumulator = 0.0f;
//toggled from the ImGui window
bool useGpu = false;
//how long one physics step took, in milliseconds
float physicsMs = 0.0f;


int main()
{

    int firstSpheres = 5;
    int addBalls = 1;

    std::vector<Sphere> spheres = Engine.genSpheres(firstSpheres);
    std::vector<GLfloat> verticies = parseOBJForSphereVerticies();


    GLFWwindow* window = Render.glfwSetup(screenWidth, screenHeight, "Spheres!");

    Render.init(verticies, floorV, sizeof(floorV)/sizeof(GLfloat), verticies.size());

    Render.imguiSetup(window);

    //compile the compute shader, needs OpenGL 4.3+
    bool gpuAvailable = gpuInit(glfwGetProcAddress);
    //main while loop
    while(!glfwWindowShouldClose(window))
    {

        //get getting delta time in order to update pysics based
        //on time passed reguardless of framerate
        //helps us get smooth motion between frames
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        //add to physics accumulator every frame, this helps us only move at a fixed delta time//

        physicsAccumulator += deltaTime;

        // FPS counter
        static float timer = 0.0f;
        static int frames = 0;

        frames++;
        timer += deltaTime;

        //clear screen and change the color 
        glClearColor(1.0f, 0.6f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Settings");
        ImGui::Text("GUI");
        ImGui::SliderFloat("Camera Speed", &Camera.speed, 1.0f, 100.0f);
        //ImGui::SliderFloat("Gravity", &gravity, 0.0f, -30.0f);
        ImGui::SliderFloat("FOV", &Camera.fov, 30.0f, 120.0f);
        ImGui::SliderInt("Add Balls", &addBalls, 1, 500);
        if (gpuAvailable)
            ImGui::Checkbox("Use GPU (compute shader)", &useGpu);
        else
            ImGui::Text("GPU collisions unavailable (needs OpenGL 4.3)");
        ImGui::Text("Physics time: %.2f ms", physicsMs);
        ImGui::Text("Sphere Count: %zu", spheres.size());
        ImGui::Text("Sphere Y Position: %.2f", spheres[0].spherePos.y);
        ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
        ImGui::End();

        

        Camera.processInput(window, deltaTime);

        //activate shader program that we built earlier
        GLuint shaderApp = Render.getShaderApp();
        glUseProgram(shaderApp);

        //matrix projections using the MVP translation pipeline, the model will put our
        //sphere from its own cordinates to a space that we will use, it starts at 0,0,0
        //the view matrix will use look at to orient the camera to the correct view.
        //projection uses fov in radians along with aspect ratio and our plane
        //cutoffs to help us define the 3d space we are in. it makes sure we have depth perspective.

        glm::mat4 view =        Camera.getViewMatrix();
        glm::mat4 projection =  Camera.getProjectionMatrix();
        
        //here we are sending the matricies to the GPU using a pointer to where they start
        //sglUniformMatrix4fv(glGetUniformLocation(shaderApp, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(shaderApp, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderApp, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS) {
            if (!bKeyPressed) {
                Engine.addSpheres(spheres, addBalls);
                bKeyPressed = true;
            }
        } 
        else {
            bKeyPressed = false;
        }


        //render here
        //make sure you swap the buffers!!!!
        
        Render.renderFloor();
        Render.renderSpheres(spheres);
        //time the physics step so CPU vs GPU can be compared
        double physicsStart = glfwGetTime();
        int physicsSteps = (int)(physicsAccumulator / 0.016f);
        Engine.runPhysics(spheres, physicsAccumulator, useGpu);
        if (physicsSteps > 0)
            physicsMs = (float)((glfwGetTime() - physicsStart) * 1000.0 / physicsSteps);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        glfwPollEvents();    
    }
    
    ////clean up vertex buffer and vertex array on gpu after all work is done
    Render.cleanUp(); 
    Render.imguiCleanup();
    gpuCleanup();

    glfwTerminate();
    return 0;
}


