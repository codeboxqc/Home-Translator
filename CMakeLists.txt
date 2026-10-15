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

# Find vcpkg packages
find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SDL3 REQUIRED)
find_package(imgui CONFIG REQUIRED)

# --- Find vcpkg ImGui Bindings ---
# vcpkg provides the backend .cpp files but doesn't compile them. We must find them.
find_path(IMGUI_BACKENDS_DIR 
    NAMES imgui_impl_sdl3.cpp 
    PATH_SUFFIXES imgui/backends imgui
)

set(IMGUI_BACKEND_SOURCES
    "${IMGUI_BACKENDS_DIR}/imgui_impl_sdl3.cpp"
    "${IMGUI_BACKENDS_DIR}/imgui_impl_opengl3.cpp"
)

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
    ${IMGUI_BACKENDS_DIR} # Include the bindings folder
)

# --- Project Sources ---
set(APP_SOURCES
    "Home Translator.cpp"  
    "ai_engine.cpp"
    "app_ui.cpp"
    "doc_parser.cpp"
    "model_downloader.cpp"
    ${IMGUI_BACKEND_SOURCES} # Compile the ImGui bindings
)

add_executable(HomeTranslator ${APP_SOURCES})

# Target Link Libraries (Dependencies)
target_link_libraries(HomeTranslator PRIVATE
    ${CURL_LIBRARIES}
    OpenGL::GL
    SDL3::SDL3
    imgui::imgui   # Link the core ImGui library
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
