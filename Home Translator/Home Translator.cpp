/* ========================================================================== */
/* Home Translator.cpp                                                        */
/* Complete Font Support: Hindi, Bengali, Korean, Hebrew, Arabic, Persian, etc*/
/* ========================================================================== */
#define _CRT_SECURE_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

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

    // Comprehensive font loader covering Hindi, Bengali, Korean, Arabic, Persian, Hebrew, Thai, CJK
    void setup_unicode_fonts(ImGuiIO& io) {
        constexpr float FONT_SIZE = 22.0f;

        // 1x oversample keeps 35,000+ glyph atlas within GPU texture limits
        ImFontConfig base_cfg;
        base_cfg.OversampleH = 1;
        base_cfg.OversampleV = 1;
        base_cfg.PixelSnapH = true;

        ImFontConfig merge_cfg = base_cfg;
        merge_cfg.MergeMode = true;

        // 1. Latin, Cyrillic, Greek, Punctuation (includes ZWNJ 0x200C and ZWJ 0x200D)
        static const ImWchar latin_ranges[] = {
            0x0020, 0x024F, // Basic Latin + Latin Supplement + Extended-A & B
            0x0370, 0x052F, // Greek + Cyrillic
            0x1E00, 0x1EFF, // Latin Extended Additional
            0x2000, 0x206F, // General Punctuation (ZWNJ / ZWJ for Arabic/Indic)
            0x2070, 0x209F, // Superscripts / Subscripts
            0x20A0, 0x20CF, // Currency Symbols
            0
        };

        // 2. Hindi (Devanagari), Bengali, and Indic family
        static const ImWchar indic_ranges[] = {
            0x0900, 0x097F, // Devanagari (Hindi, Marathi, Nepali, Sanskrit)
            0x0980, 0x09FF, // Bengali (Bangla, Assamese)
            0x0A00, 0x0A7F, // Gurmukhi (Punjabi)
            0x0A80, 0x0AFF, // Gujarati
            0x0B00, 0x0B7F, // Oriya
            0x0B80, 0x0BFF, // Tamil
            0x0C00, 0x0C7F, // Telugu
            0x0C80, 0x0CFF, // Kannada
            0x0D00, 0x0D7F, // Malayalam
            0x0D80, 0x0DFF, // Sinhala
            0xA8E0, 0xA8FF, // Devanagari Extended
            0
        };

        // 3. Korean (Hangul Syllables and Jamo)
        static const ImWchar korean_ranges[] = {
            0x1100, 0x11FF, // Hangul Jamo
            0x3130, 0x318F, // Hangul Compatibility Jamo
            0xA960, 0xA97F, // Hangul Jamo Extended-A
            0xAC00, 0xD7AF, // Complete 11,172 modern Hangul syllables
            0xD7B0, 0xD7FF, // Hangul Jamo Extended-B
            0
        };

        // 4. Arabic and Persian / Farsi (includes گ, چ, پ, ژ, ک, ی) & Urdu
        static const ImWchar arabic_persian_ranges[] = {
            0x0600, 0x06FF, // Basic Arabic & Persian letters
            0x0750, 0x077F, // Arabic Supplement
            0x08A0, 0x08FF, // Arabic Extended-A
            0xFB50, 0xFDFF, // Arabic Presentation Forms-A (contextual shapes & ligatures)
            0xFE70, 0xFEFF, // Arabic Presentation Forms-B
            0
        };

        // 5. Hebrew
        static const ImWchar hebrew_ranges[] = {
            0x0590, 0x05FF, // Hebrew
            0xFB1D, 0xFB4F, // Hebrew Presentation Forms
            0
        };

        // 6. Thai & Lao
        static const ImWchar thai_lao_ranges[] = {
            0x0E00, 0x0E7F, // Thai
            0x0E80, 0x0EFF, // Lao
            0
        };

        // 7. CJK Unified (Chinese & Japanese Kana)
        static const ImWchar cjk_ranges[] = {
            0x3000, 0x303F, // CJK Symbols and Punctuation
            0x3040, 0x309F, // Hiragana
            0x30A0, 0x30FF, // Katakana
            0x3100, 0x312F, // Bopomofo
            0x4E00, 0x9FFF, // CJK Unified Ideographs
            0
        };

        // 8. Other Scripts (Georgian, Armenian, Myanmar, Ethiopic, Khmer)
        static const ImWchar other_ranges[] = {
            0x1000, 0x109F, // Myanmar
            0x10A0, 0x10FF, // Georgian
            0x1200, 0x137F, // Ethiopic
            0x1780, 0x17FF, // Khmer
            0
        };

#if defined(_WIN32)
        // 1. Base Latin / Cyrillic font
        const std::vector<std::string> base_fonts = {
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\tahoma.ttf"
        };
        bool loaded = try_load_font(io, base_fonts, FONT_SIZE, &base_cfg, latin_ranges);

        // 2. Merge Indic (Nirmala UI covers Hindi, Bengali, Marathi, Nepali, Tamil, etc.)
        const std::vector<std::string> indic_fonts = {
            "C:\\Windows\\Fonts\\nirmala.ttf", // Windows 8/10/11 master Indic font
            "C:\\Windows\\Fonts\\mangal.ttf",  // Devanagari fallback
            "C:\\Windows\\Fonts\\vrinda.ttf",  // Bengali fallback
            "C:\\Windows\\Fonts\\shonar.ttf"
        };
        try_load_font(io, indic_fonts, FONT_SIZE, &merge_cfg, indic_ranges);

        // 3. Merge Korean (Malgun Gothic)
        const std::vector<std::string> korean_fonts = {
            "C:\\Windows\\Fonts\\malgun.ttf",  // Standard Windows Korean font
            "C:\\Windows\\Fonts\\gulim.ttc",
            "C:\\Windows\\Fonts\\batang.ttc"
        };
        try_load_font(io, korean_fonts, FONT_SIZE, &merge_cfg, korean_ranges);

        // 4. Merge Arabic & Persian / Farsi (Tahoma & Arial have complete Arabic/Farsi shaping)
        const std::vector<std::string> arabic_fonts = {
            "C:\\Windows\\Fonts\\tahoma.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\seguiui.ttf",
            "C:\\Windows\\Fonts\\times.ttf"
        };
        try_load_font(io, arabic_fonts, FONT_SIZE, &merge_cfg, arabic_persian_ranges);

        // 5. Merge Hebrew
        const std::vector<std::string> hebrew_fonts = {
            "C:\\Windows\\Fonts\\tahoma.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\david.ttf",
            "C:\\Windows\\Fonts\\gisha.ttf"
        };
        try_load_font(io, hebrew_fonts, FONT_SIZE, &merge_cfg, hebrew_ranges);

        // 6. Merge Thai & Lao
        const std::vector<std::string> thai_fonts = {
            "C:\\Windows\\Fonts\\leelawad.ttf",
            "C:\\Windows\\Fonts\\LeelawUI.ttf",
            "C:\\Windows\\Fonts\\tahoma.ttf"
        };
        try_load_font(io, thai_fonts, FONT_SIZE, &merge_cfg, thai_lao_ranges);

        // 7. Merge CJK (Chinese & Japanese)
        const std::vector<std::string> cjk_fonts = {
            "C:\\Windows\\Fonts\\msyh.ttc",   // Microsoft YaHei
            "C:\\Windows\\Fonts\\simsun.ttc",
            "C:\\Windows\\Fonts\\meiryo.ttc",
            "C:\\Windows\\Fonts\\msgothic.ttc"
        };
        try_load_font(io, cjk_fonts, FONT_SIZE, &merge_cfg, cjk_ranges);

        // 8. Merge Georgian, Armenian & others
        const std::vector<std::string> other_fonts = {
            "C:\\Windows\\Fonts\\sylfaen.ttf", // Sylfaen has Georgian & Armenian
            "C:\\Windows\\Fonts\\khmer.ttf",
            "C:\\Windows\\Fonts\\laoui.ttf"
        };
        try_load_font(io, other_fonts, FONT_SIZE, &merge_cfg, other_ranges);

#else
        // Linux / Unix fallback using Noto Sans family
        const std::vector<std::string> base_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
        };
        bool loaded = try_load_font(io, base_fonts, FONT_SIZE, &base_cfg, latin_ranges);

        const std::vector<std::string> indic_fonts = {
            "/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSansBengali-Regular.ttf"
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
        try_load_font(io, thai_fonts, FONT_SIZE, &merge_cfg, thai_lao_ranges);

        const std::vector<std::string> cjk_fonts = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
        };
        try_load_font(io, cjk_fonts, FONT_SIZE, &merge_cfg, cjk_ranges);
#endif

        if (!loaded) {
            base_cfg.SizePixels = FONT_SIZE;
            io.Fonts->AddFontDefault(&base_cfg);
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
        1360, 860, window_flags
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

    setup_unicode_fonts(io);
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.FramePadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ScrollbarSize = 18.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.WindowRounding = 6.0f;

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