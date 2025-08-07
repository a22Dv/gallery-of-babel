#include "glb_app.hpp"
#include "stb_image.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_int/import_export.hpp>
#include <climits>
#include <cstdint>
#include <format>
#include <hello_imgui/app_window_params.h>
#include <hello_imgui/docking_params.h>
#include <hello_imgui/hello_imgui.h>
#include <hello_imgui/imgui_theme.h>
#include <hello_imgui/imgui_window_params.h>
#include <hello_imgui/runner_params.h>
#include <hello_imgui/screen_bounds.h>
#include <imgui.h>
#include <random>
#include <filesystem>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")


/*
    TODO: Fix weird looking icon issues. Finish additional controls, variable speed seeking. Add actual core 
    display functionality. 
*/
namespace glb {
constexpr const int rgbaChannels{4};
constexpr const int icoSize{32};

namespace {
    std::filesystem::path getAssetDir() {
        std::string execDir{};
        execDir.resize(512);
        GetModuleFileNameA(NULL, execDir.data(), 512);
        std::filesystem::path execPath{execDir};
        execPath.remove_filename();
        execPath.append("assets\\");
        return execPath;
    }
}
void Application::postInit() {
    /*
        This is Windows-specific. Honestly the default white title bar clashes with the application's theme
        in my opinion so we set it manually here.
    */
    GLFWwindow *window{static_cast<GLFWwindow *>(HelloImGui::GetRunnerParams()->backendPointers.glfwWindow)};
    HWND hwnd{glfwGetWin32Window(window)};
    COLORREF darkColor = 0x00252322;
    DwmSetWindowAttribute(hwnd, DWMWINDOWATTRIBUTE::DWMWA_CAPTION_COLOR, &darkColor, sizeof(darkColor));

    std::filesystem::path assetDir{getAssetDir()};

    int icoX{}, icoY{}, icoCh{};
    assetDir.append("gll_icon.png");
    void* img = stbi_load(assetDir.string().c_str(), &icoX, &icoY, &icoCh, rgbaChannels);
    GLFWimage icon{icoSize, icoSize, static_cast<unsigned char*>(img)};
    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(img);
};

void Application::update() {
    if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
        state.showInputPanel = !state.showInputPanel;
        for (HelloImGui::DockableWindow &window : HelloImGui::GetRunnerParams()->dockingParams.dockableWindows) {
            window.isVisible = state.showInputPanel;
        }
    }
}
void Application::additionalControlWindow() {
    if (!state.showInputPanel) {
        return;
    }
    ImGui::SliderScalar(
        "##", ImGuiDataType_::ImGuiDataType_U64, &state.sliderIdx, &state.minSlider, &state.maxSlider, ""
    );
}

void Application::controlWindow() {
    if (!state.showInputPanel) {
        return;
    }
    /*
       Got these digits from Wolfram-Alpha's online calculator. It's probably correct.
       From 7.1795000302083x10^6,658,301. The maximum number of all 720p RGB images.
   */
    constexpr const double maxMantissa{7.1795000302083};
    const double mantissa{maxMantissa * (static_cast<double>(state.sliderIdx) / state.maxSlider)};

    const std::string labelText{std::format("Image no. {}... x 10^6,658,301", mantissa)};
    const std::string buttonText{"I'm Feeling Lucky!"};

    float buttonX{ImGui::CalcTextSize(buttonText.c_str()).x};
    float imgNumX{ImGui::CalcTextSize(labelText.c_str()).x};
    float spacing{ImGui::GetContentRegionMax().x - (buttonX + imgNumX) - 16.0f};
    ImGui::Text("%s", labelText.c_str());
    ImGui::SameLine(0.0f, spacing);
    if (ImGui::Button(buttonText.c_str())) {
        randomGen();
    }
    ImGui::PushItemWidth(-1);
    if (ImGui::SliderScalar(
            "##", ImGuiDataType_::ImGuiDataType_U64, &state.sliderIdx, &state.minSlider, &state.maxSlider, ""
        )) {
        sliderInterpolate();
    }
    ImGui::PopItemWidth();
}

Application::Application() {
    rParams.callbacks.ShowGui = [this] { update(); };
    rParams.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    rParams.callbacks.PostInit = [this] { postInit(); };
    rParams.dockingParams.dockingSplits.push_back(
        HelloImGui::DockingSplit{"MainDockSpace", "MainBottom", ImGuiDir_Down, 0.12f}
    );
    rParams.dockingParams.dockingSplits.push_back(
        HelloImGui::DockingSplit{"MainBottom", "MainBottomRight", ImGuiDir_Right, 0.5f}
    );

    HelloImGui::DockableWindow controlWindow{"Controls", "MainBottom", [this] { this->controlWindow(); }, true, false};
    HelloImGui::DockableWindow additionalCWindow{
        "Additional Controls", "MainBottomRight", [this] { this->additionalControlWindow(); }, true, false
    };
    rParams.dockingParams.dockableWindows = {additionalCWindow, controlWindow};

    HelloImGui::AppWindowParams &aWParams = rParams.appWindowParams;
    aWParams.windowTitle = title;
    aWParams.windowGeometry.size = HelloImGui::ScreenSize{1280, 720};
    aWParams.windowGeometry.fullScreenMode = HelloImGui::FullScreenMode::NoFullScreen;
    aWParams.windowGeometry.sizeAuto = false;
    aWParams.resizable = false;
}

void Application::run() { HelloImGui::Run(rParams); }

void Application::randomGen() {
    constexpr const std::size_t uint64Sz{sizeof(std::uint64_t) * CHAR_BIT};
    const std::size_t totalBits{mp::msb(state.maxImgIdx) + 1};
    static std::random_device rd{};
    static std::mt19937_64 gen{rd()};
    std::vector<std::uint64_t> rdChunks{};
    rdChunks.resize((mp::msb(state.maxImgIdx) + 1) / uint64Sz);
    while (true) {
        /*
            This stuff only works because we use 1280x720x3, which is divisible by 64.
            If you do change the resolution, take note of this too.
        */
        for (std::uint64_t &chunk : rdChunks) {
            chunk = gen();
        }
        mp::import_bits(state.imgIdx, rdChunks.begin(), rdChunks.end());
        if (state.imgIdx >= 0 && state.imgIdx <= state.maxImgIdx) {
            break;
        }
    }
    /*
        Might be fragile. Just take note for possible errors.
        >> 1 is required due to the fact that ImGui uses doubles internally
        and cannot represent all of uint64_t in full precision.
    */
    state.sliderIdx = ((state.imgIdx >> (totalBits - uint64Sz))).convert_to<std::uint64_t>() >> 1;
}

void Application::sliderInterpolate() {
    constexpr const std::size_t uint64Sz{sizeof(std::uint64_t) * CHAR_BIT};
    const std::size_t totalBits{mp::msb(state.maxImgIdx) + 1};
    /*
          We have to interpolate this in such a roundabout way because it freezes when you use
          multiplication and division on such a huge number. We only interpolate the top 64 bits of the number.
      */
    state.imgIdx = mp::cpp_int{state.sliderIdx << 1} << totalBits - uint64Sz;
}
} // namespace glb