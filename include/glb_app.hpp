#pragma once

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/multiprecision/detail/default_ops.hpp>
#include <hello_imgui/runner_params.h>
#include <string>
#include <cstddef>

namespace glb {

namespace mp = boost::multiprecision;

struct ApplicationState {
    const std::uint64_t width{1280};
    const std::uint64_t height{720};
    const std::uint64_t bPixel{24}; // 8 bits per channel, RGB.
    const std::uint64_t minSlider{0};
    const std::uint64_t maxSlider{UINT64_MAX / 2};
    const mp::cpp_int maxImgIdx{mp::pow(mp::cpp_int{2}, width * height * bPixel) - 1};
    std::size_t totalLimbs{};
    mp::cpp_int imgIdx{};
    std::uint64_t sliderIdx{};
    std::string path{};
    bool showInputPanel{true};
};

class Application {
  private:
    ApplicationState state{};
    const std::string title{"Gallery of Babel"};
    HelloImGui::RunnerParams rParams{};
    void postInit();
    void randomGen();
    void controlWindow();
    void additionalControlWindow();
    void update();
    void sliderInterpolate();

  public:
    void run();
    Application();
};

} // namespace glb
