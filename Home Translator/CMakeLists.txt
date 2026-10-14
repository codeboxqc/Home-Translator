cmake_minimum_required(VERSION 3.18)
project(HomeTranslator LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Dependencies
find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(CURL REQUIRED) # Native vcpkg libcurl

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui" CACHE PATH "Path to Dear ImGui")

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(HomeTranslator
    "Home Translator.cpp"
    ai_engine.cpp
    app_ui.cpp
    doc_parser.cpp
    model_downloader.cpp
    infer_core.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(HomeTranslator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(HomeTranslator PRIVATE
    SDL3::SDL3
    OpenGL::GL
    CURL::libcurl
)

if(WIN32)
    target_link_libraries(HomeTranslator PRIVATE opengl32 ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(HomeTranslator PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()