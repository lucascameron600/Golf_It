#ifndef ENGINE_H
#define ENGINE_H
#include <vector>
#include <glm/glm.hpp>
#include "sphere.h"


class Engine{
public:
    Engine();

    void performVerlet(Sphere& sphere);
    void putForce(Sphere& sphere, glm::vec3 force, float mass);
    void checkCollision(Sphere& sphere1, Sphere& sphere2);
    void floorCollision(Sphere& sphere, float floorY = 0.0f);
    void boundaryCollision(Sphere& sphere, float boundaryMinx, float boundaryMaxx, float boundaryMiny, float boundaryMaxy, float boundaryMinz, float boundaryMaxz);
    void addSpheres(std::vector<Sphere>& spheres, int count);
    std::vector<Sphere> genSpheres(int numSpheres);
    void runPhysics(std::vector<Sphere>& spheres, float& physicsAcc, bool useGpu = false);
    void runPhysicsMultithread(std::vector<Sphere>& spheres, float& physicsAcc);

private:
    // packed x, y, z, radius for every sphere, sent to the GPU
    std::vector<float> gpuData;
    void collideGpu(std::vector<Sphere>& spheres);

    float boundaryMaxx = 50.0f;
    float boundaryMinx = -50.0f;

    float boundaryMaxy = 50.0f;
    float boundaryMiny = 0.0f;

    float boundaryMaxz = 50.0f;
    float boundaryMinz = -50.0f;

    float gravity = -9.85;
    const float fixedDeltaTime = 0.016f;
};
#endif
