cmake_minimum_required(VERSION 3.16)
project(HomeTranslator CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler optimization flags
if(MSVC)
    add_compile_options(/O2 /W4 /MP /utf-8)
else()
    add_compile_options(-O3 -Wall -Wextra -pthread)
endif()

# Find CURL
find_package(CURL REQUIRED)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Find SDL3
find_package(SDL3 REQUIRED)

# --- Download libinfer files ---
set(LIBINFER_URL_BASE "https://raw.githubusercontent.com/codeboxqc/libinfer/21adde4")
set(LIBINFER_DIR "${CMAKE_CURRENT_BINARY_DIR}/libinfer")

file(MAKE_DIRECTORY ${LIBINFER_DIR})
file(DOWNLOAD "${LIBINFER_URL_BASE}/infer.h" "${LIBINFER_DIR}/infer.h" SHOW_PROGRESS)

if(WIN32)
    file(DOWNLOAD "${LIBINFER_URL_BASE}/infer.lib" "${LIBINFER_DIR}/infer.lib" SHOW_PROGRESS)
    file(DOWNLOAD "${LIBINFER_URL_BASE}/infer.dll" "${CMAKE_CURRENT_BINARY_DIR}/infer.dll" SHOW_PROGRESS)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${SDL3_INCLUDE_DIRS}
    ${LIBINFER_DIR}
)

# ImGui Source Files (Place imgui files in imgui/ or current directory)
file(GLOB IMGUI_SOURCES
    "imgui/*.cpp"
    "imgui/backends/imgui_impl_sdl3.cpp"
    "imgui/backends/imgui_impl_opengl3.cpp"
)

# Fallback if ImGui is placed in root folder
if(NOT IMGUI_SOURCES)
    file(GLOB IMGUI_SOURCES
        "imgui.cpp"
        "imgui_draw.cpp"
        "imgui_tables.cpp"
        "imgui_widgets.cpp"
        "imgui_impl_sdl3.cpp"
        "imgui_impl_opengl3.cpp"
    )
endif()

# Project Sources
set(APP_SOURCES
    "Home Translator.cpp"
    "ai_engine.cpp"
    "app_ui.cpp"
    "doc_parser.cpp"
    "model_downloader.cpp"
    ${IMGUI_SOURCES}
)

add_executable(HomeTranslator ${APP_SOURCES})

# Target Link Libraries
target_link_libraries(HomeTranslator PRIVATE
    ${CURL_LIBRARIES}
    OpenGL::GL
    SDL3::SDL3
)

# --- Link libinfer ---
if(WIN32)
    target_link_libraries(HomeTranslator PRIVATE "${LIBINFER_DIR}/infer.lib" ws2_32)
else()
    target_link_libraries(HomeTranslator PRIVATE infer pthread dl)
endif()

# Copy models folder next to binary
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/models")
