#include "glb_app.hpp"
#include <exception>
#include <iostream>

int main() {
    try {
        glb::Application app{};
        app.run();
    } catch (const std::exception& e) {
        std::cerr << e.what();
    }
}