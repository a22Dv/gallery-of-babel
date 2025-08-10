cmake_minimum_required(VERSION 3.22)
project(gallery_of_babel CXX)
find_package(hello-imgui CONFIG REQUIRED)

set (
    SRC_FILES
    src/main.cpp
    src/glb_app.cpp
    src/resource.rc
)

add_executable(gallery_of_babel WIN32)

target_sources(gallery_of_babel PRIVATE ${SRC_FILES})
target_compile_features(gallery_of_babel PRIVATE cxx_std_20)
target_link_libraries(gallery_of_babel PRIVATE hello-imgui::hello_imgui)
 # Explicitly enable exception handling. Required by clangd.
target_compile_options(gallery_of_babel PRIVATE "/fp:precise")
target_compile_options(gallery_of_babel PRIVATE /EHsc)
target_include_directories(gallery_of_babel PRIVATE "${CMAKE_SOURCE_DIR}/include")
