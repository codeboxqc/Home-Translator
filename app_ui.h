/* ========================================================================== */
/* app_ui.h                                                                   */
/* Header for UI with Layout Mode, Throttled Metrics & Rounded Controls       */
/* ========================================================================== */
#ifndef APP_UI_H
#define APP_UI_H

#include "ai_engine.h"
#include "doc_parser.h"
#include "model_downloader.h"

#include <cstdint>
#include <string>
#include <vector>

class AppUI {
public:
    explicit AppUI(AIEngine* engine);
    ~AppUI();

    void render();
    void handle_file_drop(const char* dropped_filepath);

private:
    void scan_available_models();
    int count_installed_official_presets() const;
    const std::vector<std::string>& get_active_model_supported_languages() const;
    void update_active_languages();

    void render_donation_button();
    void render_download_modal();
    void render_header_panel();
    void render_controls_panel();
    void render_progress_and_cancel_bar();
    void render_text_editors_panel();
    void render_status_bar();

    // Layout directive helper
    void apply_layout_directive();

    // Fast word counter
    static size_t count_words(const std::string& str);

    AIEngine* m_engine = nullptr;
    ModelDownloader m_downloader;

    std::vector<std::string> m_available_models;
    int m_selected_model_idx = 0;

    char m_doc_path_buf[512] = "";
    std::string m_input_text;
    std::string m_custom_directive;

    // Toggles
    bool m_auto_correct_on_translate = true;
    bool m_layout_formatting = false; // Layout Text ON vs Bypass OFF

    // Metrics & Throttling
    std::string m_output_text;
    uint64_t m_output_revision{ 0 };
    size_t m_cached_input_words{ 0 };
    size_t m_cached_input_chars{ 0 };
    size_t m_cached_output_words{ 0 };
    size_t m_cached_output_chars{ 0 };
    size_t m_last_input_size{ 0 };
    double m_last_word_count_time{ 0.0 };
    bool m_was_generating{ false };

    std::vector<const char*> m_active_lang_ptrs;
    int m_in_lang_idx = 0;
    int m_out_lang_idx = 1;
    int m_input_source_idx = 0; // 0: Window Text, 1: Clipboard, 2: File
    int m_ctx_size = 4096;

    bool m_show_download_modal = false;
    bool m_auto_scroll = true;
    std::string m_status_msg;
    bool m_status_is_error = false;
};

#endif // APP_UI_H