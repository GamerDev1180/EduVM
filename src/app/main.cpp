// EduVM desktop application: GLFW window + Dear ImGui (OpenGL 3) front end
// over eduvm_core.

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>  // InputEventsQueue: keep rendering while typed keys are pending

#include <GLFW/glfw3.h>

#include <cstdio>
#include <memory>

#include "app/App.hpp"
#include "app/Platform.hpp"
#include "app/Theme.hpp"
#include "app/VmScreen.hpp"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#endif

using namespace eduvm::app;

namespace {

void onGlfwError(int code, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", code, description); }

// Installed before ImGui's callbacks, which chain to it. Keys go to the VM
// screen that has keyboard focus (as scancodes, independent of host layout).
void onKey(GLFWwindow*, int key, int scancode, int action, int mods) { VmScreen::handleKey(key, scancode, action, mods); }

void showFatal(const char* message) {
#ifdef _WIN32
    MessageBoxA(nullptr, message, "EduVM", MB_ICONERROR | MB_OK);
#else
    std::fprintf(stderr, "%s\n", message);
#endif
}

}  // namespace

int main() {
    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) {
        showFatal("Could not initialise the window system.");
        return 1;
    }

#ifdef __APPLE__
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1440, 900, "EduVM", nullptr, nullptr);
    if (!window) {
        showFatal("Could not create a window. Your graphics driver needs OpenGL 3.0 or newer.");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
#ifdef _WIN32
    setDialogOwner(glfwGetWin32Window(window));
#endif

    float scale = 1.0f;
    glfwGetWindowContentScale(window, &scale, nullptr);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // layout is fixed; nothing worth persisting
    loadFonts(scale);
    applyTheme(scale);

    glfwSetKeyCallback(window, onKey);  // must precede ImGui so ImGui chains to it
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    int exitCode = 0;
    try {
        auto app = std::make_unique<App>(scale, window);
        while (true) {
            // ImGui applies queued key events one per frame, so never sleep while
            // input is still pending (fast typing or pasted keystrokes).
            const bool inputPending = ImGui::GetCurrentContext()->InputEventsQueue.Size > 0;
            if (app->wantsContinuousFrames() || inputPending) glfwPollEvents();
            else glfwWaitEventsTimeout(0.25);  // idle: sleep until input, but keep VM status fresh

            if (app->exitApproved()) break;
            if (glfwWindowShouldClose(window)) {
                if (app->requestExit()) break;
                glfwSetWindowShouldClose(window, GLFW_FALSE);
            }
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
                glfwWaitEventsTimeout(0.25);
                continue;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            app->frame();
            ImGui::Render();

            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.086f, 0.094f, 0.114f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }
    } catch (const std::exception& e) {
        showFatal(e.what());
        exitCode = 1;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
