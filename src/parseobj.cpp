#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include "parseobj.h"
//#include <glad/gl.h>
//#include <GLFW/glfw3.h>

std::vector<GLfloat> parseOBJForSphereVerticies() {
    
    std::vector<GLfloat> verticies;
    std::vector<GLfloat> finalVerticies;
    std::ifstream file("sphere2.obj");

    std::string line;

    if (file.is_open()) {
        while (std::getline(file, line)) {

            std::stringstream stream(line);
            std::string type;
            stream >> type;

            if (type == "v") {

                float x, y, z;
                stream >> x >> y >> z;

                verticies.push_back(x);
                verticies.push_back(y);
                verticies.push_back(z);

            } else if (type == "f") {

                std::string v1, v2, v3;
                stream >> v1 >> v2 >> v3;
    
                for (const std::string& v : {v1, v2, v3}) {

                    int index = std::stoi(v.substr(0, v.find('/'))) - 1;
                    int offset = index *3;
                    finalVerticies.push_back(verticies[offset]);
                    finalVerticies.push_back(verticies[offset + 1]);
                    finalVerticies.push_back(verticies[offset + 2]);
                }
            }
        }
        file.close();

    } else {
        std::cout << "failed obj file" << std::endl;
    }

    return finalVerticies;
}
