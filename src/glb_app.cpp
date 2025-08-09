#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "glb_app.hpp"
#include "stb_image.h"

#define cimg_display 0
#include "CImg.h"
#include <algorithm>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_int/import_export.hpp>
#include <boost/multiprecision/detail/default_ops.hpp>
#include <boost/multiprecision/detail/min_max.hpp>
#include <climits>
#include <cmath>
#include <cstdint>
#include <filesystem>
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


#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <libloaderapi.h>

#pragma comment(lib, "dwmapi.lib")

/*
    TODO: Add file loading functionality.
*/

namespace glb {

constexpr const int rgbaChannels{4};
constexpr const int icoSize{32};
const double log2_10{std::log2f(10)};
constexpr const std::uint64_t maxB2{22'118'400}; // 1280 * 720 * 8 * 3, number of bits in a 720p image.

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

} // namespace

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
    assetDir.append("gll_32x32.png");
    void *img = stbi_load(assetDir.string().c_str(), &icoX, &icoY, &icoCh, rgbaChannels);
    GLFWimage icon{icoSize, icoSize, static_cast<unsigned char *>(img)};
    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(img);

    // Texture creation.
    glGenTextures(1, &state.textureData.textureId);
    glBindTexture(GL_TEXTURE_2D, state.textureData.textureId);

    // Filtering and wrapping.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Reserve & unbind.
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGB, imgWidth, imgHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, state.textureData.texture.data()
    );
    glBindTexture(GL_TEXTURE_2D, 0);
};

void Application::updateTexture() {
    static mp::cpp_int cachedIdx{state.imgIdx};
    static SpatialInterpretation cachedSp{state.spInterp};
    static ColorSpaceInterpretation cachedClr{state.clrInterp};
    static std::vector<std::uint8_t> exportBuffer(imgHeight * imgWidth * imgCh);
    if (cachedIdx == state.imgIdx && cachedSp == static_cast<SpatialInterpretation>(state.spInterp) &&
        cachedClr == static_cast<ColorSpaceInterpretation>(state.clrInterp)) {
        return;
    }
    auto interleavedToPlanar{[&] {
        const std::size_t imgSize{imgHeight * imgWidth};
        for (std::size_t i = 0; i < imgSize; ++i) {
            for (std::size_t j = 0; j < imgCh; ++j) {
                const std::uint8_t value{exportBuffer[i * imgCh + j]};
                state.textureData.texture[imgSize * j + i] = value;
            }
        }
    }};
    switch (static_cast<SpatialInterpretation>(state.spInterp)) {
    case SpatialInterpretation::INTERLEAVED:
        mp::export_bits(state.imgIdx, state.textureData.texture.begin(), CHAR_BIT);
        break;
    case SpatialInterpretation::INTERLEAVED_REVERSED:
        mp::export_bits(state.imgIdx, state.textureData.texture.begin(), CHAR_BIT);
        std::reverse(state.textureData.texture.begin(), state.textureData.texture.end());
        break;
    case SpatialInterpretation::PLANAR:
        mp::export_bits(state.imgIdx, exportBuffer.begin(), CHAR_BIT);
        interleavedToPlanar();
        break;
    case SpatialInterpretation::PLANAR_REVERSED:
        mp::export_bits(state.imgIdx, exportBuffer.begin(), CHAR_BIT);
        interleavedToPlanar();
        std::reverse(state.textureData.texture.begin(), state.textureData.texture.end());
        break;
    case SpatialInterpretation::GRAY_CODE: {
        const mp::cpp_int gImg{state.imgIdx ^ (state.imgIdx >> 1)};
        mp::export_bits(gImg, state.textureData.texture.begin(), CHAR_BIT);
        break;
    }
    default: break;
    }
    using namespace cimg_library;
    switch (static_cast<ColorSpaceInterpretation>(state.clrInterp)) {
    case ColorSpaceInterpretation::HSV: {
        CImg<std::uint8_t> hsv{state.textureData.texture.data(), imgWidth, imgHeight, 1, imgCh, true};
        hsv.HSVtoRGB();
        break;
    }
    case ColorSpaceInterpretation::YCBCR: {
        CImg<std::uint8_t> ycbr{state.textureData.texture.data(), imgWidth, imgHeight, 1, imgCh, true};
        ycbr.YCbCrtoRGB();
        break;
    }
    default: break;
    }
    cachedIdx = state.imgIdx;
    cachedSp = static_cast<SpatialInterpretation>(state.spInterp);
    cachedClr = static_cast<ColorSpaceInterpretation>(state.clrInterp);
}

void Application::update() {
    if (ImGui::IsKeyPressed(ImGuiKey_H, false)) {
        state.showPanels = !state.showPanels;
        for (HelloImGui::DockableWindow &window : HelloImGui::GetRunnerParams()->dockingParams.dockableWindows) {
            window.isVisible = state.showPanels;
        }
    }
    updateTexture();
    glBindTexture(GL_TEXTURE_2D, state.textureData.textureId);
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, imgWidth, imgHeight, GL_RGB, GL_UNSIGNED_BYTE, state.textureData.texture.data()
    );
    ImDrawList *bgDrawList{ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())};
    bgDrawList->AddImage(static_cast<ImTextureID>(state.textureData.textureId), ImVec2{0, 0}, ImVec2{1280, 720});
}

void Application::beforeExit() {
    if (state.textureData.textureId) {
        glDeleteTextures(1, &state.textureData.textureId);
        state.textureData.textureId = 0;
    }
}

void Application::additionalControlWindow() {
    if (!state.showPanels) {
        return;
    }
    ImGui::PushItemWidth(-1);
    if (ImGui::SliderScalar(
            "##", ImGuiDataType_::ImGuiDataType_U64, &state.jumpSliderIdx, &state.minSlider,
            &state.maxJumpIntervalSlider, std::format("Approx. Interval: 1x10^{}", state.jumpSliderIdx).c_str()
        )) {
        state.jumpIntervalIdx = mp::pow(mp::cpp_int{10}, state.jumpSliderIdx);
    }
    ImGui::SliderInt(
        "##x", &state.spInterp, 0, static_cast<int>(SpatialInterpretation::COUNT) - 1,
        spGetStr(static_cast<SpatialInterpretation>(state.spInterp))
    );
    ImGui::SliderInt(
        "##xx", &state.clrInterp, 0, static_cast<int>(ColorSpaceInterpretation::COUNT) - 1,
        clrGetStr(static_cast<ColorSpaceInterpretation>(state.clrInterp))
    );
    ImGui::PopItemWidth();
}

void Application::controlWindow() {
    /*
       Got these digits from Wolfram-Alpha's online calculator. It's probably correct.
       From 7.1795000302083x10^6,658,301. The maximum number of all 720p RGB images.
   */
    constexpr const double maxMantissa{7.1795000302083};
    const double mantissa{maxMantissa * (static_cast<double>(state.coarseSliderIdx) / state.maxCoarseSlider)};

    const std::string labelText{std::format("Image #{}... x 10^6,658,301", mantissa)};
    const std::string buttonText{"I'm Feeling Lucky!"};

    float buttonX{ImGui::CalcTextSize(buttonText.c_str()).x};
    float imgNumX{ImGui::CalcTextSize(labelText.c_str()).x};
    float spacing{ImGui::GetContentRegionMax().x - (buttonX + imgNumX) - 16.0f};
    if (ImGui::Button(buttonText.c_str(), ImVec2{-1, 0})) {
        randomGen();
        idxInterpolate();
    }
    ImGui::PushItemWidth(-1);
    if (ImGui::SliderScalar(
            "##", ImGuiDataType_::ImGuiDataType_U64, &state.coarseSliderIdx, &state.minSlider, &state.maxCoarseSlider,
            labelText.c_str()
        )) {
        /*
            This is such a humongous number you might as well randomly fill in the lower bits, else
            you'll just see plain black.
        */
        randomGen();
        state.imgIdx >>= 64;
        state.imgIdx |= mp::cpp_int{state.coarseSliderIdx}
                        << ((mp::msb(state.maxImgIdx) + 1) - (sizeof(std::uint64_t) * CHAR_BIT));
    }
    ImGui::PopItemWidth();
    float availableWidth{ImGui::GetContentRegionAvail().x};
    float intervalButtonWidth{availableWidth / 2 - 5.0f};
    if (ImGui::Button("<<", ImVec2{intervalButtonWidth, 0}) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
        state.imgIdx = mp::max(mp::cpp_int{0}, state.imgIdx - state.jumpIntervalIdx);
        idxInterpolate();
    }
    ImGui::SameLine();
    if (ImGui::Button(">>", ImVec2{intervalButtonWidth, 0}) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
        state.imgIdx = mp::min(state.maxImgIdx, state.imgIdx + state.jumpIntervalIdx);
        idxInterpolate();
    }
}

Application::Application() {
    rParams.callbacks.ShowGui = [this] { update(); };
    rParams.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    rParams.callbacks.PostInit = [this] { postInit(); };
    rParams.dockingParams.dockingSplits.push_back(
        HelloImGui::DockingSplit{"MainDockSpace", "MainBottom", ImGuiDir_Down, 0.14f}
    );
    rParams.dockingParams.dockingSplits.push_back(
        HelloImGui::DockingSplit{"MainBottom", "MainBottomRight", ImGuiDir_Right, 0.5f}
    );
    rParams.imGuiWindowParams.tweakedTheme.Theme = ImGuiTheme::ImGuiTheme_BlackIsBlack;
    rParams.imGuiWindowParams.backgroundColor = ImVec4{1.0f, 1.0f, 1.0f, 1.0f};

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
}

void Application::idxInterpolate() {
    constexpr const std::size_t uint64Sz{sizeof(std::uint64_t) * CHAR_BIT};
    const std::size_t totalBits{mp::msb(state.maxImgIdx) + 1};
    /*
        Might be fragile. Just take note for possible errors.
        >> 1 is required due to the fact that ImGui uses doubles internally
        and cannot represent all of uint64_t in full precision.
    */
    state.coarseSliderIdx = ((state.imgIdx >> (totalBits - uint64Sz))).convert_to<std::uint64_t>() >> 1;
}

} // namespace glb