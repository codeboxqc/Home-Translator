/* ========================================================================== */
/* app_ui.h                                                                   */
/* Clean, Popup-Free, 100% Responsive UI Layout                               */
/* ========================================================================== */
#ifndef APP_UI_H
#define APP_UI_H

#include "ai_engine.h"
#include "doc_parser.h"
#include "epub.h"
#include "model_downloader.h"

#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

struct DiffToken {
    enum Type { Same, Removed, Added } type;
    std::string text;
};

class AppUI {
public:
    explicit AppUI(AIEngine* engine);
    ~AppUI();

    void render();
    void handle_file_drop(const char* dropped_filepath);
    void trigger_hotkey_clipboard_translate();

private:
    void scan_available_models();
    int count_installed_official_presets() const;
    const std::vector<std::string>& get_active_model_supported_languages() const;
    void update_active_languages();


    void start_epub_format_translation();
    EpubArchiveState m_epub_state;
    bool m_is_headless_epub_task = false;
    std::string m_headless_epub_out_path;


    void dispatch_ai_task(AITaskMode mode, bool is_retry = false);
    void rebuild_diff_tokens();

    void render_donation_button();
    void render_download_window();
    void render_header_panel();
    void render_controls_panel();
    void render_glossary_drawer();
    void render_progress_and_cancel_bar();
    void render_text_editors_panel();
    void render_inline_diff_view();
    void render_status_bar();

    void apply_layout_directive();
    static size_t count_words(const std::string& str);

    AIEngine* m_engine = nullptr;
    ModelDownloader m_downloader;

    std::vector<std::string> m_available_models;
    int m_selected_model_idx = 0;

    char m_doc_path_buf[512] = "";
    char m_export_path_buf[512] = "translated_output.txt";
    std::string m_input_text;
    std::string m_custom_directive;
    std::string m_glossary_text;
    std::vector<SubtitleCue> m_loaded_srt_cues;

    // Pro Toggles & Task State
    AITaskMode m_last_task_mode = AITaskMode::Translate;
    bool m_auto_correct_on_translate = true;
    bool m_layout_formatting = false;
    bool m_enable_continuity = false;
    bool m_enable_tm_cache = true;
    bool m_qwen3_deep_think = false;
    bool m_preserve_code_keys = false;
    bool m_show_glossary_drawer = false;
    bool m_show_inline_diff = false;

    // Output, Diff Cache & Metrics
    std::string m_output_text;
    std::vector<DiffToken> m_diff_tokens;
    uint64_t m_output_revision{ 0 };
    uint64_t m_diff_computed_revision{ 0 };
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
    int m_input_source_idx = 0;
    int m_ctx_size = 0;

    // Non-blocking download window toggle
    bool m_show_download_window = false;
    bool m_auto_scroll = true;
    std::string m_status_msg;
    bool m_status_is_error = false;

    // Async doc loading state
    std::mutex m_file_load_mutex;
    std::atomic<bool> m_file_load_pending{ false };
    std::string m_async_loaded_content;
    std::vector<SubtitleCue> m_async_loaded_cues;
    std::string m_async_loaded_path;
    DocParser::FileType m_async_loaded_ft;
    std::string m_async_loaded_error;
};

#endif // APP_UI_H