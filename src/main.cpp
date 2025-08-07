#include "glb_app.hpp"
#include <Windows.h>
#include <exception>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow) {
    try {
        glb::Application app{};
        app.run();
    } catch (const std::exception &e) {
        MessageBoxA(NULL, e.what(), "Application Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}