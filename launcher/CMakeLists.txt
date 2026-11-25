# peanuts-native: the launcher (SDL3 + Dear ImGui). Builds for aarch64 (the AppImage) and for the build machine (screenshots in CI).
cmake_minimum_required(VERSION 3.22)
project(peanuts_native_launcher CXX C)
set(IMGUI_DIR "" CACHE PATH "Dear ImGui sources")
set(STB_DIR "" CACHE PATH "stb (stb_image.h)")
set(SDL_PREFIX "" CACHE PATH "SDL3 install prefix")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(peanuts-native src/ui.cpp src/core.cpp src/steam.cpp
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(peanuts-native PRIVATE src ${IMGUI_DIR} ${STB_DIR} ${SDL_PREFIX}/include)
target_link_directories(peanuts-native PRIVATE ${SDL_PREFIX}/lib)
target_compile_options(peanuts-native PRIVATE -O2 -g -Wall -Wno-unused-function)
target_compile_definitions(peanuts-native PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(peanuts-native PRIVATE SDL3 pthread)
target_link_options(peanuts-native PRIVATE -Wl,-rpath,$ORIGIN/../lib)
