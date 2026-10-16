/* ========================================================================== */
/* Home Translator.cpp                                                        */
/* Multi-Script Fonts, Global Hotkey (Ctrl+Alt+T) & Eco Idle Frame Throttling */
/* ========================================================================== */
#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <stdint.h>

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
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_video.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include "infer.h"
#include "doc_parser.h"
#include "ai_engine.h"
#include "app_ui.h"

namespace {
    bool try_load_font(
        ImGuiIO& io,
        const std::vector<std::string>& candidates,
        float size,
        const ImFontConfig* cfg,
        const ImWchar* ranges
    ) {
        for (const auto& path : candidates) {
            if (fs::exists(path)) {
                io.Fonts->AddFontFromFileTTF(path.c_str(), size, cfg, ranges);
                return true;
            }
        }
        return false;
    }

    void setup_unicode_fonts(ImGuiIO& io) {
        constexpr float FONT_SIZE = 20.0f;

        ImFontConfig base_cfg;
        base_cfg.OversampleH = 1;
        base_cfg.OversampleV = 1;
        base_cfg.PixelSnapH = true;

        ImFontConfig merge_cfg = base_cfg;
        merge_cfg.MergeMode = true;

        static const ImWchar latin_ranges[] = {
            0x0020, 0x024F, 0x0370, 0x052F, 0x1E00, 0x1EFF,
            0x2000, 0x206F, 0x2070, 0x209F, 0x20A0, 0x20CF, 0
        };

        static const ImWchar indic_ranges[] = {
            0x0900, 0x097F, 0x0980, 0x09FF, 0x0A00, 0x0A7F, 0x0A80, 0x0AFF,
            0x0B00, 0x0B7F, 0x0B80, 0x0BFF, 0x0C00, 0x0C7F, 0x0C80, 0x0CFF,
            0x0D00, 0x0D7F, 0x0D80, 0x0DFF, 0xA8E0, 0xA8FF, 0
        };

        static const ImWchar korean_ranges[] = {
            0x1100, 0x11FF, 0x3130, 0x318F, 0xA960, 0xA97F, 0xAC00, 0xD7AF, 0xD7B0, 0xD7FF, 0
        };

        static const ImWchar arabic_persian_ranges[] = {
            0x0600, 0x06FF, 0x0750, 0x077F, 0x08A0, 0x08FF, 0xFB50, 0xFDFF, 0xFE70, 0xFEFF, 0
        };

        static const ImWchar hebrew_ranges[] = {
            0x0590, 0x05FF, 0xFB1D, 0xFB4F, 0
        };

        static const ImWchar thai_ranges[] = {
            0x0E00, 0x0E7F, 0
        };

        static const ImWchar myanmar_ranges[] = {
            0x1000, 0x109F, 0xA9E0, 0xA9FF, 0xAA60, 0xAA7F, 0
        };

        static const ImWchar cjk_ranges[] = {
            0x3000, 0x303F, 0x3040, 0x309F, 0x30A0, 0x30FF, 0x3100, 0x312F, 0x4E00, 0x9FFF, 0
        };

        static const ImWchar other_ranges[] = {
            0x10A0, 0x10FF, 0x1200, 0x137F, 0x0530, 0x058F, 0xFB13, 0xFB17, 0
        };

#if defined(_WIN32)
        const std::vector<std::string> base_fonts = {
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\tahoma.ttf",
            "fonts/segoeui.ttf"
        };
        bool loaded = try_load_font(io, base_fonts, FONT_SIZE, &base_cfg, latin_ranges);

        const std::vector<std::string> indic_fonts = {
            "C:\\Windows\\Fonts\\nirmala.ttf",
            "C:\\Windows\\Fonts\\mangal.ttf",
            "C:\\Windows\\Fonts\\vrinda.ttf",
            "fonts/nirmala.ttf"
        };
        try_load_font(io, indic_fonts, FONT_SIZE, &merge_cfg, indic_ranges);

        const std::vector<std::string> korean_fonts = {
            "C:\\Windows\\Fonts\\malgun.ttf",
            "C:\\Windows\\Fonts\\gulim.ttc",
            "fonts/malgun.ttf"
        };
        try_load_font(io, korean_fonts, FONT_SIZE, &merge_cfg, korean_ranges);

        const std::vector<std::string> arabic_fonts = {
            "C:\\Windows\\Fonts\\tahoma.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf"
        };
        try_load_font(io, arabic_fonts, FONT_SIZE, &merge_cfg, arabic_persian_ranges);

        const std::vector<std::string> hebrew_fonts = {
            "C:\\Windows\\Fonts\\tahoma.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\david.ttf"
        };
        try_load_font(io, hebrew_fonts, FONT_SIZE, &merge_cfg, hebrew_ranges);

        const std::vector<std::string> thai_fonts = {
            "C:\\Windows\\Fonts\\leelawad.ttf",
            "C:\\Windows\\Fonts\\LeelawUI.ttf",
            "C:\\Windows\\Fonts\\tahoma.ttf"
        };
        try_load_font(io, thai_fonts, FONT_SIZE, &merge_cfg, thai_ranges);

        const std::vector<std::string> myanmar_fonts = {
            "C:\\Windows\\Fonts\\mmrtext.ttf",
            "C:\\Windows\\Fonts\\mmrtextb.ttf",
            "C:\\Windows\\Fonts\\padauk.ttf",
            "C:\\Windows\\Fonts\\pyidaungsu.ttf",
            "fonts/mmrtext.ttf",
            "fonts/NotoSansMyanmar-Regular.ttf"
        };
        try_load_font(io, myanmar_fonts, FONT_SIZE, &merge_cfg, myanmar_ranges);

        const std::vector<std::string> cjk_fonts = {
            "C:\\Windows\\Fonts\\msyh.ttc",
            "C:\\Windows\\Fonts\\simsun.ttc",
            "C:\\Windows\\Fonts\\meiryo.ttc"
        };
        try_load_font(io, cjk_fonts, FONT_SIZE, &merge_cfg, cjk_ranges);

        const std::vector<std::string> other_fonts = {
            "C:\\Windows\\Fonts\\sylfaen.ttf",
            "C:\\Windows\\Fonts\\nyala.ttf"
        };
        try_load_font(io, other_fonts, FONT_SIZE, &merge_cfg, other_ranges);
#else
        const std::vector<std::string> base_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "fonts/NotoSans-Regular.ttf"
        };
        bool loaded = try_load_font(io, base_fonts, FONT_SIZE, &base_cfg, latin_ranges);

        const std::vector<std::string> indic_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSansBengali-Regular.ttf",
            "fonts/NotoSansDevanagari-Regular.ttf"
        };
        try_load_font(io, indic_fonts, FONT_SIZE, &merge_cfg, indic_ranges);

        const std::vector<std::string> korean_fonts = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansKR-Regular.otf",
            "/usr/share/fonts/truetype/noto/NotoSansKR-Regular.ttf"
        };
        try_load_font(io, korean_fonts, FONT_SIZE, &merge_cfg, korean_ranges);

        const std::vector<std::string> arabic_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf"
        };
        try_load_font(io, arabic_fonts, FONT_SIZE, &merge_cfg, arabic_persian_ranges);

        const std::vector<std::string> hebrew_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSansHebrew-Regular.ttf"
        };
        try_load_font(io, hebrew_fonts, FONT_SIZE, &merge_cfg, hebrew_ranges);

        const std::vector<std::string> thai_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSansThai-Regular.ttf",
            "/usr/share/fonts/truetype/thai/Waree.ttf"
        };
        try_load_font(io, thai_fonts, FONT_SIZE, &merge_cfg, thai_ranges);

        const std::vector<std::string> myanmar_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSansMyanmar-Regular.ttf",
            "/usr/share/fonts/truetype/padauk/Padauk.ttf",
            "/usr/share/fonts/truetype/padauk/Padauk-Regular.ttf",
            "/usr/share/fonts/opentype/noto/NotoSansMyanmar-Regular.otf",
            "fonts/NotoSansMyanmar-Regular.ttf"
        };
        try_load_font(io, myanmar_fonts, FONT_SIZE, &merge_cfg, myanmar_ranges);

        const std::vector<std::string> cjk_fonts = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
        };
        try_load_font(io, cjk_fonts, FONT_SIZE, &merge_cfg, cjk_ranges);

        const std::vector<std::string> other_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSansGeorgian-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSansArmenian-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSansEthiopic-Regular.ttf"
        };
        try_load_font(io, other_fonts, FONT_SIZE, &merge_cfg, other_ranges);
#endif

        if (!loaded) {
            base_cfg.SizePixels = FONT_SIZE;
            io.Fonts->AddFontDefault(&base_cfg);
        }
    }

    void setup_modern_theme() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 10.0f;
        style.ChildRounding = 8.0f;
        style.FrameRounding = 8.0f;
        style.PopupRounding = 8.0f;
        style.ScrollbarRounding = 9.0f;
        style.GrabRounding = 6.0f;
        style.TabRounding = 6.0f;
        style.FrameBorderSize = 1.0f;
        style.WindowBorderSize = 1.0f;
        style.FramePadding = ImVec2(10.0f, 7.0f);
        style.ItemSpacing = ImVec2(10.0f, 8.0f);
        style.ScrollbarSize = 16.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.55f, 0.65f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.12f, 0.16f, 0.85f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.12f, 0.16f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.18f, 0.22f, 0.30f, 0.70f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.16f, 0.22f, 0.80f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.23f, 0.32f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.28f, 0.38f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.14f, 0.19f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.09f, 0.12f, 0.75f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.07f, 0.08f, 0.10f, 0.50f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.26f, 0.36f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.26f, 0.34f, 0.48f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.32f, 0.42f, 0.58f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.24f, 0.85f, 0.58f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.65f, 0.95f, 0.90f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.75f, 1.00f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.15f, 0.20f, 0.28f, 0.90f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.29f, 0.40f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.37f, 0.52f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.15f, 0.21f, 0.30f, 0.80f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.30f, 0.44f, 0.90f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.27f, 0.37f, 0.54f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.18f, 0.22f, 0.30f, 0.60f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.25f, 0.32f, 0.45f, 0.78f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.30f, 0.40f, 0.58f, 1.00f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.55f, 0.90f, 0.25f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.25f, 0.65f, 0.98f, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.30f, 0.72f, 1.00f, 0.95f);
        colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.16f, 0.22f, 0.86f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.30f, 0.42f, 0.90f);
        colors[ImGuiCol_TabActive] = ImVec4(0.17f, 0.23f, 0.34f, 1.00f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.20f, 0.65f, 0.95f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 0.75f, 1.00f, 1.00f);
    }
} // anonymous namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
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
        "Local AI Translation & Publishing Suite Pro",
        1420, 880, window_flags
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
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // FIX 1: Disable 60-second automatic memory compaction crash
    io.ConfigMemoryCompactTimer = -1.0f;

    setup_unicode_fonts(io);
    setup_modern_theme();

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    AIEngine ai_engine;
    ai_engine.init();

    AppUI app_ui(&ai_engine);

#if defined(_WIN32)
    // Register Global OS Hotkey: Ctrl + Alt + T (ID = 0x4001)
    RegisterHotKey(nullptr, 0x4001, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T');
#endif

    // FIX 2: Precise 60 FPS / Eco Idle Throttling
    constexpr uint64_t TARGET_FRAME_NS = 1000000000ULL / 60ULL; // 60 FPS (~16.6 ms)
    auto prev_frame_time = std::chrono::steady_clock::now();

    bool running = true;
    while (running) {
#if defined(_WIN32)
        MSG win_msg;
        while (PeekMessage(&win_msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
            if (win_msg.wParam == 0x4001) {
                SDL_RaiseWindow(window);
                app_ui.trigger_hotkey_clipboard_translate();
            }
        }
#endif

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
        glClearColor(0.08f, 0.09f, 0.12f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        // Frame throttle to prevent 100% CPU/GPU core pegging & TDR crash
        auto current_time = std::chrono::steady_clock::now();
        uint64_t elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(current_time - prev_frame_time).count();
        if (elapsed_ns < TARGET_FRAME_NS) {
            uint64_t sleep_ms = (TARGET_FRAME_NS - elapsed_ns) / 1000000ULL;
            if (sleep_ms > 0) {
                SDL_Delay(static_cast<uint32_t>(sleep_ms));
            }
        }
        prev_frame_time = std::chrono::steady_clock::now();
    }

#if defined(_WIN32)
    UnregisterHotKey(nullptr, 0x4001);
#endif

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