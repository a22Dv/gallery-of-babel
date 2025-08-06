#include "glb_app.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_int/import_export.hpp>
#include <climits>
#include <cstdint>
#include <format>
#include <hello_imgui/app_window_params.h>
#include <hello_imgui/hello_imgui.h>
#include <hello_imgui/runner_params.h>
#include <hello_imgui/screen_bounds.h>
#include <imgui.h>
#include <random>

namespace glb {

Application::Application() {
    rParams.callbacks.ShowGui = [this] { updateGui(); };
    HelloImGui::AppWindowParams &aWParams = rParams.appWindowParams;
    aWParams.windowTitle = title;
    aWParams.windowGeometry.size = HelloImGui::ScreenSize{1280, 720};

    // Maximized window.
    aWParams.windowGeometry.fullScreenMode = HelloImGui::FullScreenMode::NoFullScreen;
    aWParams.windowGeometry.sizeAuto = false;
}

void Application::run() { HelloImGui::Run(rParams); }

void Application::updateGui() {

    ImGui::Text("%s", std::format("COUNTER").c_str());
    if (ImGui::Button("I'm Feeling Lucky!", ImVec2{})) {
        randomGen();
    }
    if (ImGui::SliderScalar(
            "##", ImGuiDataType_::ImGuiDataType_U64, &state.sliderIdx, &state.minSlider, &state.maxSlider
        )) {
        sliderInterpolate();
    }
}

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