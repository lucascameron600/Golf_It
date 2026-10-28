#include <vector>
#include "engine.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "sphere.h"
#include "gpu_physics.h"


//default ctor
Engine::Engine() {}

void Engine::performVerlet(Sphere& sphere){
    //temporary vertex to store sphere pos
    glm::vec3 temporary = sphere.spherePos;
    //main impplementation of verlet integration(predicting next step
    //based on motion)
    // changed this to not use as many temporary vectors
    glm::vec3 velocity = sphere.spherePos - sphere.prevPos;
    glm::vec3 accelerationTerm = sphere.acceleration * (fixedDeltaTime * fixedDeltaTime);
    sphere.spherePos = sphere.spherePos + velocity + accelerationTerm;
    //updates position of last frame
    sphere.prevPos = temporary;
    //makes sure the force from this fame doesent effect the next
    sphere.acceleration = glm::vec3(0.0f);
}
void Engine::putForce(Sphere& sphere, glm::vec3 force, float mass){
    sphere.acceleration += force/mass;
}
void Engine::checkCollision(Sphere& sphere1, Sphere& sphere2){
    glm::vec3 delta = sphere2.spherePos - sphere1.spherePos;
    float dist = glm::length(delta);
    float minDist = sphere1.radius + sphere2.radius;
    if (dist < minDist && dist > 0.0f)
    {
        glm::vec3 correction = delta * ((minDist - dist) / dist * 0.5f);
        sphere1.spherePos -= correction;
        sphere2.spherePos += correction;
    }
}
void Engine::floorCollision(Sphere& sphere, float floorY){
    if(sphere.spherePos.y - sphere.radius < floorY){
        glm::vec3 velocity = sphere.spherePos - sphere.prevPos;
        sphere.spherePos.y = floorY + sphere.radius;
        velocity.y *= -1;
        sphere.prevPos = sphere.spherePos - velocity;

    }
}
void Engine::boundaryCollision(Sphere& sphere, float boundaryMinx, float boundaryMaxx, float boundaryMiny, float boundaryMaxy, float boundaryMinz, float boundaryMaxz){
    glm::vec3& pos = sphere.spherePos;
    glm::vec3 vel = sphere.spherePos - sphere.prevPos;
    //glm::vec3& vel = sphere.sphereVelocity;
    //float sphere.radius = 1.0f; // Approximate radius of your sphere

    //similar to collision with the floor, we give this funciton every cordinate for our bounds
    //and it checks every sphere to make sure it is within, if it is not it sets its velocity to be negative (opposite direction)
    //very simple collision
    if (pos.x - sphere.radius < boundaryMinx) {
        pos.x = boundaryMinx + sphere.radius;
        vel.x *= -1.0f;
    } else if (pos.x + sphere.radius > boundaryMaxx) {
        pos.x = boundaryMaxx - sphere.radius;
        vel.x *= -1.0f;
    }

    
    if (pos.y - sphere.radius < boundaryMiny) {
        pos.y = boundaryMiny + sphere.radius;
        vel.y *= -1.0f;
    } else if (pos.y + sphere.radius > boundaryMaxy) {
        pos.y = boundaryMaxy - sphere.radius;
        vel.y *= -1.0f;
    }


    if (pos.z - sphere.radius < boundaryMinz) {
        pos.z = boundaryMinz + sphere.radius;
        vel.z *= -1.0f;
    } else if (pos.z + sphere.radius > boundaryMaxz) {
        pos.z = boundaryMaxz - sphere.radius;
        vel.z *= -1.0f;
    }
    sphere.prevPos = sphere.spherePos - vel;
}
void Engine::addSpheres(std::vector<Sphere>& spheres, int count){
    for (int i = 0; i < count; ++i) {
        Sphere sphere;
        sphere.spherePos = glm::vec3(rand() % 31, 5.0f, rand() % 31);
        sphere.prevPos = sphere.spherePos - glm::vec3(0.0f, 0.01f, 0.0f);
        spheres.push_back(sphere);
}
}
std::vector<Sphere> Engine::genSpheres(int numSpheres){
    std::vector<Sphere> spheres;
        int num = numSpheres;
        for (int i = 0; i < num; ++i) {
            //create sphere
            Sphere sphere;
            //sets original position of sphere right away
            sphere.spherePos = glm::vec3(i * 2.0f, 5.0f, 0.0f); 
            //initialize previous pos do not forget this line trust me
            sphere.prevPos = sphere.spherePos - glm::vec3(0.0f, 0.01f, 0.0f);
            // reset velocity to zero before pushing          
            spheres.push_back(sphere);
            }
        return spheres;
}

void Engine::collideGpu(std::vector<Sphere>& spheres){
    int n = (int)spheres.size();
    gpuData.resize(n * 4);
    //pack the spheres into a flat float array (x, y, z, radius)
    for (int i = 0; i < n; ++i) {
        gpuData[i * 4 + 0] = spheres[i].spherePos.x;
        gpuData[i * 4 + 1] = spheres[i].spherePos.y;
        gpuData[i * 4 + 2] = spheres[i].spherePos.z;
        gpuData[i * 4 + 3] = spheres[i].radius;
    }
    //every sphere checks against every other sphere in parallel on the GPU
    gpuCollide(gpuData.data(), n);
    //unpack the corrected positions
    for (int i = 0; i < n; ++i) {
        spheres[i].spherePos = glm::vec3(gpuData[i * 4 + 0], gpuData[i * 4 + 1], gpuData[i * 4 + 2]);
    }
}

void Engine::runPhysics(std::vector<Sphere>& spheres, float& physicsAcc, bool useGpu){
    while(physicsAcc >= fixedDeltaTime){
    
        for(Sphere& sphere : spheres){
            putForce(sphere, glm::vec3(0.0f, gravity, 0.0f), 0.1f);
            performVerlet(sphere);
            floorCollision(sphere, 0.0f);
            boundaryCollision(sphere, boundaryMinx, boundaryMaxx, boundaryMiny, boundaryMaxy, boundaryMinz, boundaryMaxz);
        }
        //this is O(n^2) time complexity currently which I think is a cpu bottleneck for rendering more spheres.
        //if 500 spheres are generated there will be 125000 collision checks per physics step
        if (useGpu) {
            collideGpu(spheres);
        } else {
            for (size_t i = 0; i < spheres.size(); ++i) {
                for (size_t j = i + 1; j < spheres.size(); ++j) {
                checkCollision(spheres[i], spheres[j]);
                }
            }
        }
        physicsAcc -= fixedDeltaTime;
    }
}
