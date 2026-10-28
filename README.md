# OpenGl Physics Simulation

This physics simulation is written in C++ using GLFW for the window and OpenGL as an API to render multiple particles/spheres to the screen, I also briefly write GLSL in the Vertex and Fragment shaders. ImGui is used for adjusting values and providing a minimal ui

![gif](https://github.com/user-attachments/assets/f1c2c377-bd6c-4937-b32c-8994856aee0c)
## Description

Using Verlet integration as the base numerical integrator, this project uses c++ to simulate real time physics like gravity and collision between objects. This initially started as a way to learn OpenGl but I tied it into a video I remember watching from Pezza's Work where he built something similiar with SFML. That video is linked below in the references section.  In its current state it is unoptimized so I plan on trying to implement instanced rendering and broad/narrow scope collision. This will significantly improve the number of spheres I can draw to the screen.


# Project Write up/Intro
This small C++ and OpenGL project helped me to learn a ton of new information about OpenGL, C++ and the entire 3D Graphics Pipeline. I also had to learn about classical physics and how that is typically simulated in a C++ project.
I will try to summarize what I learned here. The GPU at its simplest form can only render triangles. Therefore any other shape you see in game is typically an abstraction of the traingle or quad. everything further down the line is keeping track of where those vertices are and how to render them. The physics is based off of a method of numerical integration called verlet
integration. This means we use the particle/spheres previous position and its acceleration to predict where it will go in the future.
## Shaders/Generating a Sphere
In my last project I focused soley on learning about the OpenGL GRAPHICS PIPELINE, this pipeline consists of 6 steps starting with the VERTEX SHADER. The vertex shaders sole job depending on how you write it is to take data in the form of 
3d vertices in coordinate space and map them to a specific point on your screen. After interpolating lines between the vertices and fill space it continues to the fragment shader.The fragment shaders specific job is to figure out what color each pixel is going to be. The fragment shaders job also includes lighting in most cases. After learning about the different elements of the graphics pipeline I could begin using Vertex Buffers and arrays to send off these vertices to the gpu to be rendered, and I would get a simple triangle. As I said before, everything is an abstraction of the triangle, but I wanted a sphere. Most people proceduraly generate vertex's for their spheres using clever math but I decided to use a blender exported OBJ model of an ico-sphere(sphere composed of triangles) instead. I was hoping this would reduce the amount of computational load. After I loaded all of the vertices from each individual triangle using a parser into the vertex buffer and binded the vertex buffer to the vertex array, I could again render each triangle on the sphere. I also needed to make sure I ordered the vertex's because of the way that the GPU keeps track of rendering the triangles. 
## Generating a view
At this point I had a working unit sphere and it really was beautiful, that being said I stil couldnt move in 3D space and the sphere definetly could not move. I decided my next challenge to tackle would be the view and how that is handled in 3D space. This lead me to learning about matrix transformations, and the MVP piepline(Model, View, Projection). The MVP pipeline is a set of matrix transformations that is used to convert what is modeled in 3D space in to a set of 2D screen cordinates that are usable by your computer. The three parts of the MVP pipeline are detailed as follows. The Model Matrix takes the unit sphere in its local cordinates and transforms it to real world cordinates that can be used, we already had a model matrix in our original traingle because we need that even for a 2D representation. The View Matrix takes the world cordinates and transforms them to camera cordinates, almost like moving the whole world. We built this matrix using the camera postion, the front of the camera, and the upward direction of the camera, done using glm::lookAt. The last element is the Projection matrix, there is typically two types of projection used in 3D games orthographic and perspective, I used perspective projection. This transformation incorporates realistic 3d depth and aspect ratio. This was done using glm::perspective. After adding these transformations to your loop we must also update the vertex shader to accept these new matrixes so the gpu can perform them. Once I was done doing this and performing some light refactoring and input taking, I(and the spheres) could finally move around in 3D space. In order to get more spheres I had to further encapsulate code. After doing that I quickly rendered a floor with 2 more triangles.
## Physics
Now that I was confident I could draw one sphere I decided to tackle the task of having multiple spheres, and even having gravity and collisions between multiple spheres. This required me to learn more about how physics is typically simulated in c++. Developers commonly use Euler or Verlet integration to update object motion over time.

Euler uses a very straightforward approach where velocity and position are updated based on acceleration and velocity is explicitly tracked in the sphere or particle class, but supposedly this is an unstable way to simulate particles that are interacting often. That being said the reason I chose not to use it is more simple. Everyone that I had seen make one of these simulations had used Verlet integration so thats ultimately why I decided to use Verlet. Verlet integration only uses the previous and current frames acceleration and position. Instead of tracking velocity we can just calculate it temporarily when we need it.
I learned from others to structure my simulation so that forces like gravity could be applied as acceleration before each physics update, and then reset afterward. For collisions, I checked the distance between pairs of spheres and resolved overlaps by adjusting their positions and giving them a slight push in the opposite direction which can be adjusted. Becuase I did not use substeps in my verlet integration there is a significant amount of performance improvement left on the table at this time.

## What I am still adding
  Currently I am still working on upgrading this project by using instanced rending for my spheres. Instead of telling the GPU to draw every single vertex every frame I can instance the models, currently reading more. The other thing I am currently trying to implement is better collision. Naive collision is O(n^2) runtime which I need to reduce. Typically this is done with different phases of collision(broad/narrow) to rule out particles/spheres that will not collide.

## Final thoughts

This project not only deepened my understanding of OpenGL and the GPU pipeline but also gave me a practical lesson in linear algebra, classical physics, and real-time simulation.

It taught me how to structure a C++ application that interacts really close with the GPU. It also taught me about the structure of the GPU and exactly what it does and how. I think using a game engine to abstract all of these concepts away from me would have really taken away the joy of getting simple physical interactions to occur and I now have a deeper understanding of the foundataional concepts of graphics rendering.

### Dependencies

* Linux
* CMake
* ImGui
* OpenGl
* GLFW
* GLM
* GLSL
* GLAD
* KHR

### Installing/Executing
- Must be operated on Windows / Linux
- Install GLFW 
- sudo apt-get install libglfw3-dev
- Install GLM
- sudo apt-get install libglm-dev

### Executing program
Clone directory and inside use the command Make if you already have CMake installed
./app

### Using
Adjust sliders to your preferences
Press B to generate balls
wasd to move around on a flat plane, space to move up, shift to move down

## Authors

Lucas Cameron



## Acknowledgments

Inspiration, code snippets, etc.
https://pikuma.com/blog/verlet-integration-2d-cloth-physics-simulation
https://www.youtube.com/watch?v=lS_qeBy3aQI
https://www.youtube.com/watch?v=9IULfQH7E90&t=545s
https://github.com/johnBuffer/VerletSFML-Multithread
https://www.youtube.com/watch?v=8-B6ryuBkCM
https://github.com/kavan010/black_hole

```
code blocks for commands



![gif](https://github.com/user-attachments/assets/f1c2c377-bd6c-4937-b32c-8994856aee0c)
<img width="807" height="832" alt="image" src="https://github.com/user-attachments/assets/c2d606ba-1810-4070-80ef-1370cdacfcd6" />
<img width="815" height="643" alt="image" src="https://github.com/user-attachments/assets/9b21b9e6-339b-44a5-a218-6eaa820ec4e1" />

<img width="812" height="633" alt="image" src="https://github.com/user-attachments/assets/05e27793-141e-4434-8547-cb7ef0807589" />

<img width="813" height="838" alt="image" src="https://github.com/user-attachments/assets/dbf2a4ae-39de-4a62-a311-c80029e8977a" />

![giphy](https://github.com/user-attachments/assets/e7f99bc2-9cfe-49c9-95c2-74c7e9a7ca61)


g++ opengl.cpp sphere.cpp gl.c \
third_party/imgui/imgui.cpp \
third_party/imgui/imgui_draw.cpp \
third_party/imgui/imgui_tables.cpp \
third_party/imgui/imgui_widgets.cpp \
third_party/imgui/backends/imgui_impl_glfw.cpp \
third_party/imgui/backends/imgui_impl_opengl3.cpp \
-Iinclude \
-Ithird_party/imgui \
-Ithird_party/imgui/backends \
-lglfw -ldl -lGL -lX11 -lXxf86vm -lXcursor -lXinerama \
-g -o app



this took way longer than I expected to generate a sphere but i finally did it. main problems were simple bugs with how I set up my parser to parse the obj file that was generated by blender. I went this route insted of procedurally generating the traingle vertices because I heard it increases performance. 
