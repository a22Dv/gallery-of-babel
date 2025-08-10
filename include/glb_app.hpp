#pragma once

#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/detail/default_ops.hpp>
#include <climits>
#include <cstddef>
#include <glad/glad.h>
#include <hello_imgui/runner_params.h>
#include <string>
#include <vector>
#include <chrono>

namespace glb {

constexpr const std::uint64_t imgWidth{1280};
constexpr const std::uint64_t imgHeight{720};
constexpr const std::uint64_t imgCh{3};
namespace mp = boost::multiprecision;

enum class SpatialInterpretation : int { INTERLEAVED, INTERLEAVED_REVERSED, PLANAR, PLANAR_REVERSED, GRAY_CODE, COUNT };

enum class ColorSpaceInterpretation : int { RGB, HSV, YCBCR, COUNT };

constexpr const char *spGetStr(SpatialInterpretation sp) {
    switch (sp) {
    case SpatialInterpretation::INTERLEAVED: return "Interleaved";
    case SpatialInterpretation::INTERLEAVED_REVERSED: return "Reversed Interleaved";
    case SpatialInterpretation::PLANAR: return "Planar";
    case SpatialInterpretation::PLANAR_REVERSED: return "Reversed Planar";
    case SpatialInterpretation::GRAY_CODE: return "Gray Code";
    default: return "";
    }
}

constexpr const char *clrGetStr(ColorSpaceInterpretation clr) {
    switch (clr) {
    case ColorSpaceInterpretation::RGB: return "RGB";
    case ColorSpaceInterpretation::YCBCR: return "YCbCr";
    case ColorSpaceInterpretation::HSV: return "HSV";
    default: return "";
    }
}

struct TextureData {
    std::vector<std::uint8_t> texture{};
    GLuint textureId{};
    TextureData() : texture(std::vector<std::uint8_t>(imgWidth * imgHeight * imgCh)) {};
};

struct Notification {
    bool isActive{false};
    std::string text{};
    std::chrono::steady_clock::time_point start{};
    std::chrono::milliseconds duration{};
};

struct ApplicationState {
    const std::uint64_t minSlider{0};
    const std::uint64_t maxCoarseSlider{UINT64_MAX / 2};
    const std::uint64_t maxJumpIntervalSlider{6'658'301}; // We jump exactly 1x10^6,648,301 at maximum.
    const mp::cpp_int maxImgIdx{mp::pow(mp::cpp_int{2}, imgWidth *imgHeight *imgCh *CHAR_BIT) - 1};
    int spInterp{static_cast<int>(SpatialInterpretation::INTERLEAVED)};
    int clrInterp{static_cast<int>(ColorSpaceInterpretation::RGB)};
    std::size_t totalLimbs{};
    mp::cpp_int imgIdx{};
    mp::cpp_int jumpIntervalIdx{};
    std::uint64_t jumpSliderIdx{};
    std::uint64_t coarseSliderIdx{};
    std::string path{};
    TextureData textureData{};
    bool showPanels{true};
    bool shouldClearSentinel{};
    std::uint8_t executeIntervalCalculation{0};
};

class Application {
  private:
    Notification notif{};
    bool fWndActive;
    ApplicationState state{};
    const std::string title{"Gallery of Babel"};
    HelloImGui::RunnerParams rParams{};
    void updateTexture();
    void postInit();
    void randomGen();
    void controlWindow();
    void additionalControlWindow();
    void update();
    void idxInterpolate();
    void beforeExit();
    void toastNotif(const std::string& text, const float durationSec);
    void renderFileWindow();
    void loadFile();
    void renderNotif();
  public:
    void run();
    Application();
};

} // namespace glb
