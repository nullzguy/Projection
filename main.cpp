#include <algorithm>
#include <limits>
#include <cmath>
#include <iostream>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define PERSPECTIVE_TYPE 0
#define ORTOGRAPHIC_TYPE 1

// May conflict
using namespace std;
using namespace glm;

struct winSettings {
    // Width and height
    int w, h;
    // Frames per second & capping (simulation runs continuously, 
    // rendering happens every 1/fps seconds)
    bool capFps;
    float fps;
    // Returns the aspect
    double aspect() const { return (double)w/(double)h; };
};

struct Engine {
    GLFWwindow* window;
    winSettings sets;

    int Init(const char* name) {
        // Initialize glfw
        if (!glfwInit()) { cerr << "Failed to initialize GLFW!\n"; return 1; }
        // Hint gl version for glfw
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        // Create window
        window = glfwCreateWindow(sets.w, sets.h, name, nullptr, nullptr);
        if (window == nullptr) { cerr << "Failed to create GLFW window!\n"; glfwTerminate(); return 1; }
        glfwMakeContextCurrent(window);
        // Unlock fps
        glfwSwapInterval(0);

        return 0;
    }
};

int initImGui(Engine& e) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Style
    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 0.8f;
    style.WindowRounding = 8.0f;
    // Colors
    // example:
    // style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

    // Thanks to:
    // https://deepwiki.com/ocornut/imgui/6.3-styling-and-theming
    // https://docs.anvil.team/enums/imguicol

    // Init ImGUI
    if (!ImGui_ImplGlfw_InitForOpenGL(e.window, true)) { cerr << "Failed to initialize ImGUI for OpenGL!\n"; return 1; }
    if (!ImGui_ImplOpenGL3_Init("#version 460")) { cerr << "Failed to initialize ImGUI!\n"; return 1; }
    return 0;
}

void ImGuiFrame(Engine& e, int tps) {
    // Create new frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("Controls and stuff");
    // The whole menu
    ImGui::Checkbox("Cap FPS", &e.sets.capFps);
    if (e.sets.capFps) ImGui::SliderFloat("Max FPS", &e.sets.fps, 5.0f, 360.0f);
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("%d TPS", tps);

    ImGui::End();
    ImGui::Render();
}

void updateGlfwWindow(Engine& e) {
    // Update width, height, projection.
    glfwGetFramebufferSize(e.window, &e.sets.w, &e.sets.h);
    glViewport(0, 0, e.sets.w, e.sets.h);
    glClearColor(0.1f, 0.1f, 0.16f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-e.sets.aspect(), e.sets.aspect(), -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

struct Controls {
    double fov;
    double yaw, pitch;
    double sensX, sensY;
    double baseVel, sprintVelMultiplier;
};

// Treat it more like a "Player" or something.
struct Camera {
    dvec3 pos, dir;
    double sensorHeight;
    Controls controls;
    int type;
    // Orthographic for now

    // Camera relative dimensions & camera matrix
    // Thanks to:
    // https://www.opengl-tutorial.org/beginners-tutorials/tutorial-3-matrices/

    float focalLength() { return 1.0/(2.0*tan(controls.fov*0.5)); }
    dvec3 camRight() { return normalize(dvec3(dir.y, -dir.x, 0)); }
    dvec3 camUp() { return normalize(cross(camRight(), dir)); }
    dmat3 camMatrix() { return dmat3(camRight(), dir, camUp()); }
    dvec3 camDimRelative(dvec3 v) { return transpose(camMatrix()) * (v - pos); }

    void updateDirVector() {
        // Thanks to:
        // https://math.stackexchange.com/questions/2618527/converting-from-yaw-pitch-roll-to-vector

        double yaw = controls.yaw;
        double pitch = controls.pitch;
        
        dir = dvec3(sin(yaw)*cos(pitch), cos(yaw)*cos(pitch), sin(pitch));
    };
};

struct Triangle {
    dvec3 v1, v2, v3;

    void draw(Engine &e, Camera &cam, dvec3 col) {
        // Relative camera positions
        dvec3 rel1 = cam.camDimRelative(v1);
        dvec3 rel2 = cam.camDimRelative(v2);
        dvec3 rel3 = cam.camDimRelative(v3);
        // If behind the camera, skip rendering
        if (rel1.y <= 0 || rel2.y <= 0 || rel3.y <= 0) return;

        glBegin(GL_TRIANGLES);
        glColor3d(col.x, col.y, col.z);

        if (cam.type == PERSPECTIVE_TYPE) {
            double fl = cam.focalLength();
            for (dvec3 rel : {rel1, rel2, rel3}) {
                double invY = 1/(rel.y + fl);
                double x = (rel.x*fl)*invY;
                double y = (rel.z*fl)*invY;

                glVertex2d(x, y);
            }
        } else if (cam.type == ORTOGRAPHIC_TYPE) {
            for (dvec3 rel : {rel1, rel2, rel3}) {
                double x = dot(rel, dvec3(1.0, 0.0, 0.0)) / cam.sensorHeight;
                double y = dot(rel, dvec3(0.0, 0.0, 1.0)) / cam.sensorHeight;

                glVertex2d(x, y);
            }
        } else return;

        glEnd();
    }
};

struct Object {
    vector<Triangle> tris;
    dvec3 col;

    // Reminds me of that one rick&morty spinning bike scene
    void spin(double time) {
        double s = sin(time);
        double c = cos(time);
        for (Triangle& tri : tris) {
            for (dvec3* v : {&tri.v1, &tri.v2, &tri.v3}) {
                double x = v->x;
                double y = v->y;
                v->x = x*c - y*s;
                v->y = x*s + y*c;
            }
        }
    }
};

void doControls(Engine& e, Camera& cam, double dt) {
    double vel = cam.controls.baseVel*dt;
    if (glfwGetKey(e.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) vel*=cam.controls.sprintVelMultiplier;
    if (glfwGetKey(e.window, GLFW_KEY_S) == GLFW_PRESS) cam.pos -= cam.dir*vel;
    if (glfwGetKey(e.window, GLFW_KEY_W) == GLFW_PRESS) cam.pos += cam.dir*vel;
    if (glfwGetKey(e.window, GLFW_KEY_A) == GLFW_PRESS) cam.pos -= cam.camRight()*vel;
    if (glfwGetKey(e.window, GLFW_KEY_D) == GLFW_PRESS) cam.pos += cam.camRight()*vel;
    if (glfwGetKey(e.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) cam.pos.z -= vel;
    if (glfwGetKey(e.window, GLFW_KEY_SPACE) == GLFW_PRESS) cam.pos.z += vel;
    if (glfwGetKey(e.window, GLFW_KEY_LEFT) == GLFW_PRESS) cam.controls.yaw -= cam.controls.sensX*dt;
    if (glfwGetKey(e.window, GLFW_KEY_RIGHT) == GLFW_PRESS) cam.controls.yaw += cam.controls.sensX*dt;
    if (glfwGetKey(e.window, GLFW_KEY_DOWN) == GLFW_PRESS) cam.controls.pitch -= cam.controls.sensY*dt;
    if (glfwGetKey(e.window, GLFW_KEY_UP) == GLFW_PRESS) cam.controls.pitch += cam.controls.sensY*dt;

    if (cam.controls.pitch > M_PI*0.4999) cam.controls.pitch = M_PI*0.4999;
    if (cam.controls.pitch < -M_PI*0.4999) cam.controls.pitch = -M_PI*0.4999;
}

int main() {
    Engine engine;
    // 800x600 window, with fps capping enabled, and capped to 60.
    engine.sets = {800, 600, true, 60.0f};
    if (engine.Init((char*)"Title")!=0) return 1;
    if (initImGui(engine)!=0) return 1;

    // Controls
    Controls controls = {
        radians(80.0),
        0.0, 0.0,
        5.0, 5.0,
        2.0, 30.0
    };
    // Camera
    Camera camera = {
        {0.0, -3.0, 0.0},
        {0.0, 0.0, 0.0},
        1.0, controls,
        PERSPECTIVE_TYPE
    };
    // A crystal
    Object crystal = {  {
            {{-0.5, -0.5, 0.0}, {-0.5, 0.5, 0.0}, {0.0, 0.0, -1.5}},
            {{-0.5, 0.5, 0.0}, {0.5, 0.5, 0.0}, {0.0, 0.0, -1.5}},
            {{0.5, 0.5, 0.0}, {0.5, -0.5, 0.0}, {0.0, 0.0, -1.5}},
            {{0.5, -0.5, 0.0}, {-0.5, -0.5, 0.0}, {0.0, 0.0, -1.5}},
            {{-0.5, -0.5, 0.0}, {-0.5, 0.5, 0.0}, {0.0, 0.0, 1.5}},
            {{-0.5, 0.5, 0.0}, {0.5, 0.5, 0.0}, {0.0, 0.0, 1.5}},
            {{0.5, 0.5, 0.0}, {0.5, -0.5, 0.0}, {0.0, 0.0, 1.5}},
            {{0.5, -0.5, 0.0}, {-0.5, -0.5, 0.0}, {0.0, 0.0, 1.5}},
        }, 
        {0.6, 0.84, 0.8}
    };
    // Objects
    vector<Object> objects = {
        crystal
    };

    int ticks = 0, tps = 0;
    double tpsTimer = 0.0, renderTimer = 0.0;
    double yaw = 0.0, pitch = 0.0, 
    oldYaw=yaw-1.0, oldPitch=pitch-1;
    double oldT = glfwGetTime();
    while (!glfwWindowShouldClose(engine.window)) {
        // Compute delta time
        double currentT = glfwGetTime();
        double dt = currentT - oldT;
        oldT = currentT;
        
        // Controls
        doControls(engine, camera, dt);

        // Update cam dir vector
        if (oldPitch != camera.controls.pitch || oldYaw != camera.controls.yaw) camera.updateDirVector();
        oldYaw = camera.controls.yaw, oldPitch = camera.controls.pitch;

        // renderTimer & TPS logic
        renderTimer += dt*engine.sets.fps;
        tpsTimer += dt;
        ticks++;
        if (tpsTimer >= 1.0) {
            tpsTimer -= 1.0;
            tps = ticks;
            ticks = 0;
        }

        if (!engine.sets.capFps || renderTimer >= 1.0) {
            // Subtract back the renderTimer
            if (engine.sets.capFps) renderTimer -= 1.0;
            // Update window stuff & render
            updateGlfwWindow(engine);
            glfwPollEvents();
            ImGuiFrame(engine, tps);

            // Rendering
            for (Object obj : objects) {
                // Spin, because we can.
                obj.spin(currentT);
                for (Triangle tri : obj.tris) {
                    double colVariety = 0.25;
                    double random1 = (double)(rand()%1000000)*0.000001; // [0, 1)
                    double random2 = random1*colVariety;
                    double randomFinal = random2-random2*0.5;
                    tri.draw(engine, camera, obj.col+randomFinal);
                }
            }

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(engine.window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(engine.window);
    glfwTerminate();
    return 0;
}
