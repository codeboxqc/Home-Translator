/* ========================================================================== */
/* Home Translator.cpp                                                        */
/* Cross-Platform Main Entry Point with MSVC Safety Fixes & C++17 File Checks */
/* ========================================================================== */
#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#if defined(_MSC_VER)
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "infer.lib")
#endif
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include "infer.h"
#include "doc_parser.h"
#include "ai_engine.h"
#include "app_ui.h"

namespace {
    // Setup full Unicode font atlas for 119 languages (CJK, Cyrillic, Arabic, Thai, Extended Latin)
    void setup_unicode_fonts(ImGuiIO& io) {
        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 2;

        static const ImWchar multi_lang_ranges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin Supplement (French, Spanish, German, etc.)
            0x0100, 0x024F, // Extended Latin
            0x0370, 0x03FF, // Greek
            0x0400, 0x052F, // Cyrillic (Russian, Ukrainian, Bulgarian)
            0x0590, 0x05FF, // Hebrew
            0x0600, 0x06FF, // Arabic
            0x0E00, 0x0E7F, // Thai
            0x0900, 0x097F, // Devanagari (Hindi)
            0x2000, 0x206F, // General Punctuation
            0,
        };

        bool font_loaded = false;

#if defined(_WIN32)
        // Windows system fonts with broad Unicode & CJK coverage
        const char* win_font_paths[] = {
            "C:\\Windows\\Fonts\\msyh.ttc",    // Microsoft YaHei (Full Unicode + CJK + Cyrillic + Greek)
            "C:\\Windows\\Fonts\\simsun.ttc",  // SimSun
            "C:\\Windows\\Fonts\\segoeui.ttf", // Segoe UI
            "C:\\Windows\\Fonts\\arial.ttf"    // Arial
        };

        for (const char* path : win_font_paths) {
            if (fs::exists(path)) {
                io.Fonts->AddFontFromFileTTF(path, 18.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
                font_loaded = true;
                break;
            }
        }
#else
        // Linux system fonts
        const char* linux_font_paths[] = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"
        };

        for (const char* path : linux_font_paths) {
            if (fs::exists(path)) {
                io.Fonts->AddFontFromFileTTF(path, 18.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
                font_loaded = true;
                break;
            }
        }
#endif

        // Fallback if no system fonts were found
        if (!font_loaded) {
            config.GlyphRanges = multi_lang_ranges;
            io.Fonts->AddFontDefault(&config);
        }
    }
} // anonymous namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "Fatal Error: SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    const char* glsl_version = "#version 330";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_WindowFlags window_flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
        );

    SDL_Window* window = SDL_CreateWindow(
        "Local AI Translator & Orthography Corrector",
        1280, 800, window_flags
    );

    if (!window) {
        std::fprintf(stderr, "Fatal Error: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::fprintf(stderr, "Fatal Error: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load Multilingual Unicode Font Atlas
    setup_unicode_fonts(io);

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    AIEngine ai_engine;
    ai_engine.init();

    AppUI app_ui(&ai_engine);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
            if (event.type == SDL_EVENT_DROP_FILE) {
                app_ui.handle_file_drop(event.drop.data);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        app_ui.render();

        ImGui::Render();

        int display_w = 0, display_h = 0;
        SDL_GetWindowSizeInPixels(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.12f, 0.13f, 0.15f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ai_engine.abort();
    ai_engine.wait();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}