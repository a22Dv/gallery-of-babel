#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define cimg_display 0
#include "CImg.h"
#include "glb_app.hpp"
#include <algorithm>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_int/import_export.hpp>
#include <boost/multiprecision/detail/default_ops.hpp>
#include <boost/multiprecision/detail/min_max.hpp>
#include <chrono>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <hello_imgui/app_window_params.h>
#include <hello_imgui/docking_params.h>
#include <hello_imgui/hello_imgui.h>
#include <hello_imgui/imgui_theme.h>
#include <hello_imgui/imgui_window_params.h>
#include <hello_imgui/runner_params.h>
#include <hello_imgui/screen_bounds.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <random>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <libloaderapi.h>
#pragma comment(lib, "dwmapi.lib")

/*
    Export bits tries to "stay"
*/

namespace glb {

constexpr const int rgbaChannels{4};
constexpr const int rgbChannels{3};
constexpr const int icoSize{32};
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
    GLFWwindow *window{static_cast<GLFWwindow *>(rParams.backendPointers.glfwWindow)};
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
        mp::export_bits(state.imgIdx, exportBuffer.begin(), CHAR_BIT);
        exportBuffer[0] &= ~(state.shouldClearSentinel ? 0b1000'0000 : 0);
        state.textureData.texture = exportBuffer;
        break;
    case SpatialInterpretation::INTERLEAVED_REVERSED:
        mp::export_bits(state.imgIdx, exportBuffer.begin(), CHAR_BIT);
        exportBuffer[0] &= ~(state.shouldClearSentinel ? 0b1000'0000 : 0);
        state.textureData.texture = exportBuffer;
        std::reverse(state.textureData.texture.begin(), state.textureData.texture.end());
        break;
    case SpatialInterpretation::PLANAR:
        mp::export_bits(state.imgIdx, exportBuffer.begin(), CHAR_BIT);
        exportBuffer[0] &= ~(state.shouldClearSentinel ? 0b1000'0000 : 0);
        interleavedToPlanar();
        break;
    case SpatialInterpretation::PLANAR_REVERSED:
        mp::export_bits(state.imgIdx, exportBuffer.begin(), CHAR_BIT);
        exportBuffer[0] &= ~(state.shouldClearSentinel ? 0b1000'0000 : 0);
        interleavedToPlanar();
        std::reverse(state.textureData.texture.begin(), state.textureData.texture.end());
        break;
    case SpatialInterpretation::GRAY_CODE: {
        /*
            Bypasses sentinel bit correction logic. Left as is, as
            Gray code scrambles the index itself, and does not operate
            on some other representation of it like the other modes.
            mp::export_bits always "efficiently" skips leading zeroes.
            Visually flushing the image to the left.
        */
        const mp::cpp_int gImg{state.imgIdx ^ (state.imgIdx >> 1)};
        mp::export_bits(gImg, state.textureData.texture.begin(), CHAR_BIT);
        break;
    }
    default: break;
    }
    using namespace cimg_library;
    CImg<std::uint8_t> img{state.textureData.texture.data(), imgWidth, imgHeight, 1, imgCh, true};
    switch (static_cast<ColorSpaceInterpretation>(state.clrInterp)) {
    case ColorSpaceInterpretation::HSV: {
        img.HSVtoRGBModified();
        break;
    }
    case ColorSpaceInterpretation::YCBCR: {
        img.YCbCrtoRGB();
        break;
    }
    default: break;
    }
    cachedIdx = state.imgIdx;
    cachedSp = static_cast<SpatialInterpretation>(state.spInterp);
    cachedClr = static_cast<ColorSpaceInterpretation>(state.clrInterp);
}

void Application::update() {
    if (ImGui::IsKeyPressed(ImGuiKey_H, false) && !fWndActive) {
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
    renderNotif();
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
            &state.maxJumpIntervalSlider, std::format("Interval: 1x10^{}", state.jumpSliderIdx).c_str()
        ) ||
        state.executeIntervalCalculation != 0) {
        // We have to quit halfway before the UI freezes when it goes through with the calculation.
        if (state.jumpSliderIdx > 500'000 && !state.executeIntervalCalculation) {
            toastNotif(
                "WARNING: Larger interval values take significantly longer to calculate...\nUI will be unresponsive.",
                3.0f
            );
            state.executeIntervalCalculation = 3;
        } else if (state.executeIntervalCalculation == 1 || state.jumpSliderIdx < 500'000) {
            state.jumpIntervalIdx = mp::pow(mp::cpp_int{10}, state.jumpSliderIdx);
            state.executeIntervalCalculation--;
        } else if (state.executeIntervalCalculation != 1) {
            state.executeIntervalCalculation--;
        }
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
       From 7.1795000302083x10^6,658,301. The number of all 720p RGB images.
   */
    constexpr const double maxMantissa{7.1795000302083};
    const double mantissa{maxMantissa * (static_cast<double>(state.coarseSliderIdx) / state.maxCoarseSlider)};

    const std::string labelText{std::format("Image #{}... x 10^6,658,301", mantissa)};
    const std::string buttonAText{"I'm Feeling Lucky!"};
    const std::string buttonBText{"Image Search"};

    float buttonAX{ImGui::CalcTextSize(buttonAText.c_str()).x};
    float buttonBX{ImGui::CalcTextSize(buttonBText.c_str()).x};
    float imgNumX{ImGui::CalcTextSize(labelText.c_str()).x};
    float spacing{ImGui::GetContentRegionMax().x - (buttonAX + imgNumX) - 16.0f};
    if (ImGui::Button(buttonAText.c_str(), ImVec2{0, 0})) {
        randomGen();
        idxInterpolate();
    }
    ImGui::SameLine();
    if (ImGui::Button(buttonBText.c_str(), ImVec2{0, 0})) {
        fWndActive = true;
        ImGui::OpenPopup("Image Search");
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
    /*
        When set to the absolute maximum, the slider can only affect the lower bits. The cap given by
        min and max would then be left at the bottom, either pure black or white. 
    */
    if (ImGui::Button("<<", ImVec2{intervalButtonWidth, 0}) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
        state.imgIdx = mp::max(mp::cpp_int{0}, state.imgIdx - state.jumpIntervalIdx);
        idxInterpolate();
    }
    ImGui::SameLine();
    if (ImGui::Button(">>", ImVec2{intervalButtonWidth, 0}) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
        state.imgIdx = mp::min(state.maxImgIdx, state.imgIdx + state.jumpIntervalIdx);
        idxInterpolate();
    }
    // Weird bug where the window does not appear visible when called on the main update() loop. Hence placed here.
    renderFileWindow();
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

void Application::toastNotif(const std::string &text, const float durationSec) {
    // We throw away requests if a notification is already active.
    if (notif.isActive) {
        return;
    }
    notif.isActive = true;
    notif.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<float>(durationSec));
    notif.text = text;
    notif.start = std::chrono::steady_clock::now();
}

void Application::renderFileWindow() {
    static const std::string note{"Note:\n"
                                  "Accepts any kind of file, however only the first 2.7MB\n"
                                  "will be interpreted. In the case of .jpg/.png,the pixel data\n"
                                  "will be transformed in order to fit within the program's\n"
                                  "buffer dimensions (1280x720)."};
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2{0.5f, 0.5f});
    ImGui::SetNextWindowSize(ImVec2{ImGui::CalcTextSize(note.c_str()).x + 30.0f, 170.0f});
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4{0.0f, 0.0f, 0.0f, 0.3f});
    if (ImGui::BeginPopupModal("Image Search", &fWndActive, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::Text("File path");
        ImGui::InputText("##", &state.path);
        if (ImGui::Button("Load File") || ImGui::IsKeyDown(ImGuiKey_Enter)) {
            loadFile();
            fWndActive = false;
        }
        ImGui::Text("%s", note.c_str());
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
}

void Application::loadFile() {
    std::filesystem::path filePath{state.path};
    if (!std::filesystem::exists(filePath)) {
        toastNotif("Invalid path.", 2.0f);
    }
    std::string extension{filePath.extension().string()};
    std::vector<std::uint8_t> idxBuffer(imgWidth * imgHeight * imgCh, 0);
    if (extension == ".png" || extension == ".jpg") {
        toastNotif("Loaded as .png/.jpg.", 2.0f);
        int h{}, w{}, ch{};
        stbi_uc *imgData{stbi_load(filePath.string().c_str(), &w, &h, &ch, rgbChannels)};
        const float sH{static_cast<float>(imgHeight) / h}, sW{static_cast<float>(imgWidth) / w};
        const float scale{std::min(sH, sW)};
        const int nH{static_cast<int>(h * scale)}, nW{static_cast<int>(w * scale)};
        std::vector<std::uint8_t> buffer(nH * nW * ch);
        stbir_resize_uint8_srgb(imgData, w, h, 0, buffer.data(), nW, nH, 0, STBIR_RGB);
        stbi_image_free(static_cast<void *>(imgData));
        const std::size_t xOffset{(imgWidth - nW) / 2};
        const std::size_t yOffset{(imgHeight - nH) / 2};
        for (std::size_t y{0}; y < nH; ++y) {
            for (std::size_t x{0}; x < nW; ++x) {
                for (std::size_t ch{0}; ch < imgCh; ++ch) {
                    const std::size_t dstIdx{(yOffset + y) * imgWidth * imgCh + (xOffset + x) * imgCh + ch};
                    const std::size_t srcIdx{y * nW * imgCh + (x * imgCh) + ch};
                    idxBuffer[dstIdx] = buffer[srcIdx];
                }
            }
        }
        std::uint8_t dBit{static_cast<std::uint8_t>((idxBuffer[0] & 0b1000'0000) >> 7)};
        idxBuffer[0] |= 0b1000'0000;
        mp::import_bits(state.imgIdx, idxBuffer.begin(), idxBuffer.end());
        state.shouldClearSentinel = dBit == 0;
    } else {
        std::ifstream fileStream{filePath, std::ios::binary | std::ios::ate};
        std::streamsize fSize{fileStream.tellg()};
        fileStream.seekg(0);
        fileStream.read(
            reinterpret_cast<char *>(idxBuffer.data()), std::min(idxBuffer.size(), static_cast<std::size_t>(fSize))
        );
        toastNotif("Loaded as generic binary stream.", 2.0f);
        mp::import_bits(state.imgIdx, idxBuffer.begin(), idxBuffer.end());
    }
    idxInterpolate();
}

void Application::renderNotif() {
    if (!notif.isActive) {
        return;
    }
    ImGui::OpenPopup("notification", ImGuiPopupFlags_NoReopen);
    const std::chrono::steady_clock::time_point now{std::chrono::steady_clock::now()};
    const std::chrono::milliseconds cDuration{std::chrono::duration_cast<std::chrono::milliseconds>(now - notif.start)};
    if (cDuration < notif.duration) {
        ImGui::SetNextWindowPos(ImVec2{5.0f, 5.0f});
        if (ImGui::BeginPopup(
                "notification",
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing
            )) {
            ImGui::Text("%s", notif.text.c_str());
            ImGui::EndPopup();
        }
        return;
    }
    ImGui::CloseCurrentPopup();
    notif.isActive = false;
}

} // namespace glb