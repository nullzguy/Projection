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
// tinyObjLoader from:
// https://github.com/tinyobjloader/tinyobjloader
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define PERSPECTIVE_TYPE 0
#define ORTHOGRAPHIC_TYPE 1

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

void updateGlfwAndGl(Engine& e) {
    // Update width, height, projection, settings.
    glfwGetFramebufferSize(e.window, &e.sets.w, &e.sets.h);
    glViewport(0, 0, e.sets.w, e.sets.h);
    glClearColor(0.1f, 0.1f, 0.16f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-e.sets.aspect(), e.sets.aspect(), -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// More than just controls actually... treat as "Variables" or "Settings".
struct Controls {
    double fov, nearClipPlane;
    double yaw, pitch;
    bool useCursor;
    double xMPos, yMPos;
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
};

struct Face {
    vector<dvec3> v;
    dvec3 n;
};

struct Object {
    vector<Face> faces;
    dvec3 col;

    void draw(Camera& cam, dvec3 lightN) {
        glDepthFunc(GL_LESS);

        double fl = cam.focalLength();

        for (Face& face : faces) {
            glBegin(GL_POLYGON);

            dvec3 colFinal = col*dot(face.n, -lightN);
            size_t vcount = face.v.size();

            vector<dvec3> rels(vcount);
            for (int i = 0; i < vcount; i++) {
                rels[i] = cam.camDimRelative(face.v[i]);
            }

            glColor3d(colFinal.x, colFinal.y, colFinal.z);

            if (cam.type == PERSPECTIVE_TYPE) {
                for (dvec3 rel : rels) {
                    dvec3 m = rel * fl;
                    glVertex4d(m.x, m.z, cam.controls.nearClipPlane, m.y);
                }
            } else if (cam.type == ORTHOGRAPHIC_TYPE) {
                for (dvec3 rel : rels) {
                    double x = dot(rel, dvec3(1.0, 0.0, 0.0)) / cam.sensorHeight;
                    double z = dot(rel, dvec3(0.0, 0.0, 1.0)) / cam.sensorHeight;

                    glVertex3d(x, z, rel.y);
                }
            }
            
            glEnd();
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

    static bool qWasDown = false;
    static bool firstMouseFrame = true;
    bool qIsDown = glfwGetKey(e.window, GLFW_KEY_TAB) == GLFW_PRESS;
    if (qIsDown && !qWasDown) {
        cam.controls.useCursor = !cam.controls.useCursor;
        firstMouseFrame = true;
    }
    qWasDown = qIsDown;

    static bool lastUseCursor = false;
    if (cam.controls.useCursor != lastUseCursor) {
        glfwSetInputMode(e.window, GLFW_CURSOR, cam.controls.useCursor ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (cam.controls.useCursor && glfwRawMouseMotionSupported()) glfwSetInputMode(e.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        else glfwSetInputMode(e.window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        lastUseCursor = cam.controls.useCursor;
    }

    if (cam.controls.useCursor) {
        double oldXMPos = cam.controls.xMPos;
        double oldYMPos = cam.controls.yMPos;
        glfwGetCursorPos(e.window, &cam.controls.xMPos, &cam.controls.yMPos);

        if (firstMouseFrame) {
            firstMouseFrame = false;
        } else {
            cam.controls.yaw += cam.controls.sensX * (cam.controls.xMPos - oldXMPos) * 0.01;
            cam.controls.pitch -= cam.controls.sensY * (cam.controls.yMPos - oldYMPos) * 0.01;
        }
    }

    if (cam.controls.pitch > M_PI*0.4999) cam.controls.pitch = M_PI*0.4999;
    if (cam.controls.pitch < -M_PI*0.4999) cam.controls.pitch = -M_PI*0.4999;
}

vector<Object> loadObj(const string& path) {
    // Thanks to:
    // https://github.com/tinyobjloader/tinyobjloader

    std::string inputfile = path;
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::string warn;
    std::string err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, nullptr, &warn, &err, inputfile.c_str());
    
    if (!warn.empty()) std::cout << warn << std::endl;
    if (!err.empty()) std::cerr << err << std::endl;
    if (!ret) exit(1);

    vector<Object> out(shapes.size());

    for (size_t s = 0; s < shapes.size(); ++s) {
        const auto& mesh = shapes[s].mesh;
        out[s].faces.resize(mesh.num_face_vertices.size());
        out[s].col = {0.45, 0.45, 0.55};

        size_t index_offset = 0;

        for (size_t f = 0; f < mesh.num_face_vertices.size(); ++f) {
            const size_t fv = mesh.num_face_vertices[f];
            Face& face = out[s].faces[f];
            face.v.resize(fv);

            for (size_t v = 0; v < fv; ++v) {
                const tinyobj::index_t idx = mesh.indices[index_offset + v];
                const size_t p = 3 * idx.vertex_index;

                face.v[v] = dvec3(
                    attrib.vertices[p + 0],
                    attrib.vertices[p + 2],
                    attrib.vertices[p + 1]
                );
            }

            if (fv >= 3) {
                face.n = normalize(cross(
                    face.v[1] - face.v[0],
                    face.v[2] - face.v[0]
                ));
            } else face.n = dvec3(0.0);

            index_offset += fv;
        }
    }
    return out;
};

int main() {
    Engine engine;
    // 800x600 window, with fps capping enabled, and capped to 60.
    engine.sets = {800, 600, true, 60.0f};
    if (engine.Init((char*)"Title")!=0) return 1;
    if (initImGui(engine)!=0) return 1;

    // Controls
    Controls controls = {
        radians(80.0), 0.1,
        0.0, 0.0,
        1,
        0.0, 0.0,
        1.0, 1.0,
        2.0, 30.0
    };

    // Camera
    Camera camera = {
        {0.0, -3.0, 0.0},
        {0.0, 0.0, 0.0},
        1.0, controls,
        PERSPECTIVE_TYPE
    };

    // Objects
    vector<Object> objects = loadObj("./objects/Wolf_obj.obj");

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

        dvec3 lightN = normalize(dvec3(-1.0, -1.0, -1.0));

        if (!engine.sets.capFps || renderTimer >= 1.0) {
            // Subtract back the renderTimer
            if (engine.sets.capFps) renderTimer -= 1.0;
            // Update window stuff & render
            updateGlfwAndGl(engine);
            glfwPollEvents();
            ImGuiFrame(engine, tps);

            // Rendering
            for (Object obj : objects) {
                obj.draw(camera, lightN);
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