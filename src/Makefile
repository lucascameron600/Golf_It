
CXX = g++
CXXFLAGS = -O2 -Wall -I../include -I../libs/imgui -I../libs/imgui/backends
ifeq ($(OS),Windows_NT)
    # Windows (MSYS2 UCRT64 terminal)
    LDFLAGS = -lglfw3 -lopengl32 -lgdi32
else ifeq ($(shell uname -s),Darwin)
    # macOS (brew install glfw glm). Apple's compiler needs C++17 asked for,
    # and -Wno-deprecated quiets it about compiling gl.c as C++
    BREW := $(shell brew --prefix)
    CXXFLAGS += -std=c++17 -Wno-deprecated -I$(BREW)/include
    LDFLAGS = -L$(BREW)/lib -lglfw -framework OpenGL
else
    # Linux / WSL
    LDFLAGS = -pg -lglfw -ldl -lGL -lX11 -lXxf86vm -lXcursor -lXinerama
endif

IMGUI_SRC = \
    ../libs/imgui/imgui.cpp \
    ../libs/imgui/imgui_draw.cpp \
    ../libs/imgui/imgui_tables.cpp \
    ../libs/imgui/imgui_widgets.cpp \
    ../libs/imgui/backends/imgui_impl_glfw.cpp \
    ../libs/imgui/backends/imgui_impl_opengl3.cpp



# mini golf game (reuses the window setup, sphere model loader and glad)
GOLF_SRC = golf_main.cpp golf.cpp golf_render.cpp clubs.cpp render.cpp parseobj.cpp gl.c $(IMGUI_SRC)

all: golf editor

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

golf: $(GOLF_SRC) ../include/golf.h ../include/golf_render.h ../include/clubs.h
	$(CXX) $(CXXFLAGS) $(GOLF_SRC) -o golf $(LDFLAGS)

# level editor for designing holes (just for you, not part of the game)
EDITOR_SRC = editor_main.cpp golf.cpp golf_render.cpp golf_tools.cpp render.cpp parseobj.cpp gl.c $(IMGUI_SRC)

editor: $(EDITOR_SRC) ../include/golf.h ../include/golf_render.h ../include/golf_tools.h
	$(CXX) $(CXXFLAGS) -std=c++17 $(EDITOR_SRC) -o editor $(LDFLAGS)

clean:
	rm -f $(TARGET) golf editor $(TARGET).exe golf.exe editor.exe
