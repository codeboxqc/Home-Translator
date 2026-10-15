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

# Find vcpkg packages (Only CURL and SDL3)
find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SDL3 REQUIRED)

# --- Download and Extract libinfer Release ---
set(LIBINFER_DIR "${CMAKE_CURRENT_BINARY_DIR}/libinfer")
file(MAKE_DIRECTORY ${LIBINFER_DIR})

if(WIN32)
    set(LIBINFER_ZIP_URL "https://github.com/codeboxqc/libinfer/releases/download/latest/libinfer-windows-x64.zip")
else()
    set(LIBINFER_ZIP_URL "https://github.com/codeboxqc/libinfer/releases/download/latest/libinfer-linux-x64.zip")
endif()

set(LIBINFER_ZIP_FILE "${CMAKE_CURRENT_BINARY_DIR}/libinfer.zip")
file(DOWNLOAD "${LIBINFER_ZIP_URL}" "${LIBINFER_ZIP_FILE}" SHOW_PROGRESS)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf "${LIBINFER_ZIP_FILE}"
    WORKING_DIRECTORY ${LIBINFER_DIR}
)

# --- Include directories ---
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${SDL3_INCLUDE_DIRS}
    ${LIBINFER_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
)

# --- ImGui Source Files ---
# Explicitly list the files instead of using GLOB to prevent silent linking errors
set(IMGUI_SOURCES
    "imgui/imgui.cpp"
    "imgui/imgui_draw.cpp"
    "imgui/imgui_tables.cpp"
    "imgui/imgui_widgets.cpp"
    "imgui/backends/imgui_impl_sdl3.cpp"
    "imgui/backends/imgui_impl_opengl3.cpp"
)

# --- Project Sources ---
set(APP_SOURCES
    "Home Translator.cpp"  
    "ai_engine.cpp"
    "app_ui.cpp"
    "doc_parser.cpp"
    "model_downloader.cpp"
    ${IMGUI_SOURCES}
)

add_executable(HomeTranslator ${APP_SOURCES})

# Target Link Libraries (Dependencies)
target_link_libraries(HomeTranslator PRIVATE
    ${CURL_LIBRARIES}
    OpenGL::GL
    SDL3::SDL3
)

# --- Link and Copy libinfer Binaries ---
if(WIN32)
    target_link_libraries(HomeTranslator PRIVATE "${LIBINFER_DIR}/infer.lib" ws2_32)
    file(COPY "${LIBINFER_DIR}/infer.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
else()
    target_link_libraries(HomeTranslator PRIVATE "${LIBINFER_DIR}/libinfer.so" pthread dl)
    file(COPY "${LIBINFER_DIR}/libinfer.so" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# Copy models folder next to binary
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/models")