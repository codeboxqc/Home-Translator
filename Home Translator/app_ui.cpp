/* ========================================================================== */
/* app_ui.cpp                                                                 */
/* Clean, Popup-Free, 100% Responsive UI Layout                               */
/* ========================================================================== */
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <ctime>

#include "app_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#endif

namespace fs = std::filesystem;

std::string open_file_dialog_epub() {
#if defined(_WIN32)
    char filename[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "EPUB Books (*.epub)\0*.epub\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "epub";

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
#endif
    return "";
}

namespace {
    struct InputTextCallback_UserData {
        std::string* Str;
        ImGuiInputTextCallback ChainCallback;
        void* ChainCallbackUserData;
    };

    static int InputTextCallback(ImGuiInputTextCallbackData* data) {
        InputTextCallback_UserData* user_data = static_cast<InputTextCallback_UserData*>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            std::string* str = user_data->Str;
            str->resize(data->BufTextLen);
            data->Buf = const_cast<char*>(str->c_str());
        }
        else if (user_data->ChainCallback) {
            data->UserData = user_data->ChainCallbackUserData;
            return user_data->ChainCallback(data);
        }
        return 0;
    }

    bool InputTextMultilineStdString(
        const char* label,
        std::string* str,
        const ImVec2& size = ImVec2(0, 0),
        ImGuiInputTextFlags flags = 0
    ) {
        flags |= ImGuiInputTextFlags_CallbackResize;
        InputTextCallback_UserData cb_user_data{ str, nullptr, nullptr };
        return ImGui::InputTextMultiline(
            label,
            const_cast<char*>(str->c_str()),
            str->capacity() + 1,
            size,
            flags,
            InputTextCallback,
            &cb_user_data
        );
    }

    bool InputTextWithHintStdString(
        const char* label,
        const char* hint,
        std::string* str,
        ImGuiInputTextFlags flags = 0
    ) {
        flags |= ImGuiInputTextFlags_CallbackResize;
        InputTextCallback_UserData cb_user_data{ str, nullptr, nullptr };
        return ImGui::InputTextWithHint(
            label,
            hint,
            const_cast<char*>(str->c_str()),
            str->capacity() + 1,
            flags,
            InputTextCallback,
            &cb_user_data
        );
    }

    bool is_file_present(const std::string& filename) {
        return fs::exists("models/" + filename) || fs::exists(filename);
    }

    void open_url(const std::string& url) {
        SDL_OpenURL(url.c_str());
    }

    std::vector<std::string> tokenize_words_for_diff(const std::string& text, size_t max_words = 400) {
        std::vector<std::string> tokens;
        tokens.reserve(std::min(max_words, static_cast<size_t>(text.size() / 5 + 1)));
        size_t i = 0;
        const size_t n = text.size();
        while (i < n && tokens.size() < max_words) {
            while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            if (i >= n) break;
            size_t start = i;
            while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            tokens.emplace_back(text.data() + start, i - start);
        }
        return tokens;
    }
} // anonymous namespace

AppUI::AppUI(AIEngine* engine) : m_engine(engine) {
    m_input_text.reserve(16384);
    m_output_text.reserve(16384);
    scan_available_models();
    update_active_languages();

    int installed_presets = count_installed_official_presets();

    if (m_available_models.empty()) {
        m_show_download_window = true;
        m_status_msg = "No local models found. Open 'Models' to download official presets.";
        m_status_is_error = false;
    }
    else {
        m_engine->load_model_async(m_available_models[0], static_cast<uint32_t>(m_ctx_size));
        m_show_download_window = false;
        m_status_msg = (installed_presets < 3)
            ? "Model initialized in background. Additional presets available in 'Models'."
            : "All models present. Ready.";
        m_status_is_error = false;
    }
}

AppUI::~AppUI() {
    if (m_file_load_thread.joinable()) {
        m_file_load_thread.join();
    }
}

void AppUI::apply_layout_directive() {
    m_custom_directive =
        "Apply professional book and text layout formatting:\n"
        "1. Structure into logical, readable paragraphs.\n"
        "2. Ensure proper sentence-start capitalization.\n"
        "3. Standardize punctuation (periods, commas, em-dashes for dialogs).\n"
        "4. Preserve original story, meaning, and voice 100% faithfully.";
}

void AppUI::start_epub_format_translation() {
    std::string in_path = open_file_dialog_epub();
    if (in_path.empty()) {
        return;
    }

    std::string out_path = in_path;
    if (out_path.size() > 5 && out_path.substr(out_path.size() - 5) == ".epub") {
        out_path = out_path.substr(0, out_path.size() - 5) + "-convert.epub";
    }
    else {
        out_path += "-convert.epub";
    }
    m_headless_epub_out_path = out_path;

    m_is_headless_epub_task = true;
    m_cached_output_words = 0;
    m_cached_output_chars = 0;
    m_output_revision = 0;

    const auto& active_langs = get_active_model_supported_languages();
    AIEngineOptions opts;
    opts.mode = AITaskMode::Translate;
    opts.input_lang = active_langs[m_in_lang_idx];
    opts.output_lang = active_langs[m_out_lang_idx];
    opts.custom_directive = m_custom_directive;
    opts.glossary_raw = m_glossary_text;
    opts.auto_correct_typos = m_auto_correct_on_translate;
    opts.layout_formatting = false;
    opts.enable_continuity = m_enable_continuity;
    opts.enable_tm_cache = m_enable_tm_cache;
    opts.qwen3_deep_think = m_qwen3_deep_think;
    opts.preserve_code_keys = true;
    opts.is_retry = false;

    opts.is_epub_task = true;
    opts.epub_input_path = in_path;
    opts.epub_output_path = out_path;

    m_engine->clear_output_text();

    if (m_engine->start_task("EPUB_TASK", opts)) {
        m_status_msg = "EPUB Continuous File-by-File Converter Running: " + fs::path(in_path).filename().string() + "...";
        m_status_is_error = false;
    }
    else {
        m_status_msg = m_engine->get_last_error();
        m_status_is_error = true;
        m_is_headless_epub_task = false;
    }
}

void AppUI::scan_available_models() {
    m_available_models.clear();

    const std::vector<std::string> search_paths = { "models", "." };
    for (const auto& sp : search_paths) {
        if (fs::exists(sp) && fs::is_directory(sp)) {
            for (const auto& entry : fs::directory_iterator(sp)) {
                if (entry.is_regular_file() && entry.path().extension() == ".gguf") {
                    m_available_models.push_back(entry.path().string());
                }
            }
        }
    }
}

int AppUI::count_installed_official_presets() const {
    const auto& presets = ModelDownloader::get_presets();
    int count = 0;
    for (const auto& p : presets) {
        if (is_file_present(p.filename)) count++;
    }
    return count;
}

const std::vector<std::string>& AppUI::get_active_model_supported_languages() const {
    static const std::vector<std::string> default_langs = { "French (Français)", "English" };

    static const std::vector<std::string> llama_langs = {
        "French (Français)", "English", "German (Allemand)", "Spanish (Espagnol)",
        "Italian (Italien)", "Portuguese (Portugais)", "Hindi", "Thai (Thaï)"
    };

    static const std::vector<std::string> qwen25_langs = {
        "English", "French (Français)", "Spanish (Espagnol)", "German (Allemand)",
        "Italian (Italien)", "Portuguese (Portugais)", "Dutch (Néerlandais)", "Russian (Russe)",
        "Polish (Polonais)", "Czech (Tchèque)", "Arabic (Arabe)", "Turkish (Turc)",
        "Persian (Farsi)", "Hebrew (Hébreu)", "Chinese (Chinois)", "Japanese (Japonais)",
        "Korean (Coréen)", "Hindi", "Bengali", "Urdu", "Vietnamese (Vietnamien)",
        "Thai (Thaï)", "Indonesian (Indonésien)", "Malay (Malais)", "Tagalog (Filipino)",
        "Burmese", "Cebuano"
    };

    static const std::vector<std::string> qwen3_langs = {
        "English", "French (Français)", "Spanish (Espagnol)", "German (Allemand)",
        "Italian (Italien)", "Portuguese (Portugais)", "Dutch (Néerlandais)", "Russian (Russe)",
        "Polish (Polonais)", "Czech (Tchèque)", "Arabic (Arabe)", "Turkish (Turc)",
        "Persian (Farsi)", "Hebrew (Hébreu)", "Chinese (Chinois)", "Japanese (Japonais)",
        "Korean (Coréen)", "Hindi", "Bengali", "Urdu", "Vietnamese (Vietnamien)",
        "Thai (Thaï)", "Indonesian (Indonésien)", "Malay (Malais)", "Tagalog (Filipino)",
        "Burmese", "Cebuano", "Swedish (Suédois)", "Danish (Danois)",
        "Norwegian (Norvégien)", "Finnish (Finnois)", "Greek (Grec)", "Slovak (Slovaque)",
        "Hungarian (Hongrois)", "Romanian (Roumain)", "Bulgarian (Bulgare)", "Ukrainian (Ukrainien)",
        "Serbian (Serbe)", "Croatian (Croate)", "Lithuanian (Lituanien)", "Latvian (Letton)",
        "Estonian (Estonien)", "Tamil", "Telugu", "Marathi", "Gujarati", "Punjabi",
        "Malayalam", "Kannada", "Sinhala", "Nepali", "Pashto", "Kazakh", "Uzbek",
        "Azerbaijani (Azéri)", "Georgian (Géorgien)", "Armenian (Arménien)", "Swahili", "Afrikaans"
    };

    if (m_available_models.empty() || m_selected_model_idx >= static_cast<int>(m_available_models.size())) {
        return default_langs;
    }

    const std::string& active_path = m_available_models[m_selected_model_idx];
    if (active_path.find("Llama-3.2") != std::string::npos) {
        return llama_langs;
    }
    if (active_path.find("Qwen2.5") != std::string::npos) {
        return qwen25_langs;
    }
    return qwen3_langs;
}

void AppUI::update_active_languages() {
    m_active_lang_ptrs.clear();
    const auto& langs = get_active_model_supported_languages();
    m_active_lang_ptrs.reserve(langs.size());
    for (const auto& l : langs) {
        m_active_lang_ptrs.push_back(l.c_str());
    }

    if (m_in_lang_idx >= static_cast<int>(m_active_lang_ptrs.size())) m_in_lang_idx = 0;
    if (m_out_lang_idx >= static_cast<int>(m_active_lang_ptrs.size())) {
        m_out_lang_idx = (m_active_lang_ptrs.size() > 1) ? 1 : 0;
    }
}

size_t AppUI::count_words(const std::string& str) {
    size_t count = 0;
    bool in_word = false;
    for (unsigned char c : str) {
        if (std::isspace(c)) {
            in_word = false;
        }
        else if (!in_word) {
            count++;
            in_word = true;
        }
    }
    return count;
}

void AppUI::rebuild_diff_tokens() {
    m_diff_tokens.clear();
    auto a = tokenize_words_for_diff(m_input_text);
    auto b = tokenize_words_for_diff(m_output_text);

    const size_t n = a.size();
    const size_t m = b.size();
    if (n == 0 && m == 0) return;

    m_diff_tokens.reserve(n + m);

    std::vector<uint16_t> dp((n + 1) * (m + 1), 0);
    auto idx = [m](size_t i, size_t j) { return i * (m + 1) + j; };

    for (size_t i = n; i > 0; --i) {
        for (size_t j = m; j > 0; --j) {
            if (a[i - 1] == b[j - 1]) {
                dp[idx(i - 1, j - 1)] = static_cast<uint16_t>(1 + dp[idx(i, j)]);
            }
            else {
                dp[idx(i - 1, j - 1)] = std::max(dp[idx(i, j - 1)], dp[idx(i - 1, j)]);
            }
        }
    }

    size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) {
            m_diff_tokens.push_back({ DiffToken::Same, a[i] });
            ++i; ++j;
        }
        else if (dp[idx(i + 1, j)] >= dp[idx(i, j + 1)]) {
            m_diff_tokens.push_back({ DiffToken::Removed, a[i] });
            ++i;
        }
        else {
            m_diff_tokens.push_back({ DiffToken::Added, b[j] });
            ++j;
        }
    }
    while (i < n) m_diff_tokens.push_back({ DiffToken::Removed, a[i++] });
    while (j < m) m_diff_tokens.push_back({ DiffToken::Added, b[j++] });
    m_diff_computed_revision = m_output_revision;
}

void AppUI::handle_file_drop(const char* dropped_filepath) {
    if (!dropped_filepath) return;

    if (m_file_load_pending.load()) {
        m_status_msg = "A document is currently being loaded. Please wait...";
        m_status_is_error = true;
        return;
    }

    if (m_file_load_thread.joinable()) {
        m_file_load_thread.join();
    }

    std::string path(dropped_filepath);
    m_status_msg = "Loading document... (Please wait)";
    m_status_is_error = false;

    m_file_load_thread = std::thread([this, path]() {
        std::string error, loaded_content;
        std::vector<SubtitleCue> cues;
        bool success = DocParser::load_document(path, loaded_content, error, &cues);
        DocParser::FileType ft = DocParser::detect_file_type(path);

        std::lock_guard<std::mutex> file_lock(m_file_load_mutex);
        m_async_loaded_content = std::move(loaded_content);
        m_async_loaded_cues = std::move(cues);
        m_async_loaded_path = path;
        m_async_loaded_ft = ft;
        m_async_loaded_error = std::move(error);
        m_file_load_pending.store(true);
        });
}

void AppUI::trigger_hotkey_clipboard_translate() {
    if (m_engine->is_generating() || m_engine->is_loading_model()) return;

    char* sdl_clip = SDL_GetClipboardText();
    if (sdl_clip) {
        if (std::strlen(sdl_clip) > 0) {
            std::string clip_str(sdl_clip);
            // Remove null bytes or non-printable control characters
            clip_str.erase(std::remove(clip_str.begin(), clip_str.end(), '\0'), clip_str.end());

            if (!clip_str.empty()) {
                m_input_text = std::move(clip_str);
                m_input_source_idx = 1;
                dispatch_ai_task(AITaskMode::Translate, false);
            }
        }
        SDL_free(sdl_clip);
    }
}

void AppUI::dispatch_ai_task(AITaskMode mode, bool is_retry) {
    if (!is_retry && m_input_source_idx == 1) {
        char* sdl_clip = SDL_GetClipboardText();
        if (sdl_clip && std::strlen(sdl_clip) > 0) {
            m_input_text = sdl_clip;
            SDL_free(sdl_clip);
        }
        else {
            if (sdl_clip) SDL_free(sdl_clip);
            if (m_input_text.empty()) {
                m_status_msg = "Clipboard is empty or locked by another process!";
                m_status_is_error = true;
                return;
            }
        }
    }

    if (m_input_text.empty()) {
        m_status_msg = "Source input text is empty.";
        m_status_is_error = true;
        return;
    }

    const auto& active_langs = get_active_model_supported_languages();
    AIEngineOptions opts;
    opts.mode = mode;
    opts.input_lang = active_langs[m_in_lang_idx];
    opts.output_lang = active_langs[m_out_lang_idx];
    opts.custom_directive = m_custom_directive;
    opts.glossary_raw = m_glossary_text;
    opts.auto_correct_typos = m_auto_correct_on_translate;
    opts.layout_formatting = m_layout_formatting;
    opts.enable_continuity = m_enable_continuity;
    opts.enable_tm_cache = m_enable_tm_cache;
    opts.qwen3_deep_think = m_qwen3_deep_think;
    opts.preserve_code_keys = m_preserve_code_keys;
    opts.is_retry = is_retry;
    opts.chat_history = m_chat_history;

    m_last_task_mode = mode;

    m_output_text.clear();
    m_output_revision = 0;

    if (m_engine->start_task(m_input_text, opts)) {
        m_status_msg = is_retry ? "Retrying with boosted sampling temperature..." : "Running AI task...";
        m_status_is_error = false;
    }
    else {
        m_status_msg = m_engine->get_last_error();
        m_status_is_error = true;
    }
}

void AppUI::render() {
    // Process background model download completion regardless of whether download window is currently visible
    if (!m_downloader.is_downloading() && m_downloader.is_completed()) {
        scan_available_models();
        std::string downloaded_path = m_downloader.get_downloaded_filepath();
        for (size_t i = 0; i < m_available_models.size(); ++i) {
            if (fs::path(m_available_models[i]).filename() == fs::path(downloaded_path).filename()) {
                m_selected_model_idx = static_cast<int>(i);
                break;
            }
        }
        update_active_languages();
        m_downloader.reset_completed_flag();

        if (!downloaded_path.empty()) {
            m_engine->load_model_async(downloaded_path, static_cast<uint32_t>(m_ctx_size));
        }
    }

    apply_dynamic_theme();

    double now = ImGui::GetTime();

    // FIX: Scoped block for m_file_load_mutex locks and unlocks immediately, removing C26110 & C26117
    if (m_file_load_pending.load()) {
        std::string loaded_content;
        std::vector<SubtitleCue> loaded_cues;
        std::string loaded_path;
        DocParser::FileType loaded_ft = DocParser::FileType::UNKNOWN;
        std::string loaded_error;

        {
            std::lock_guard<std::mutex> file_lock(m_file_load_mutex);
            loaded_content = std::move(m_async_loaded_content);
            loaded_cues = std::move(m_async_loaded_cues);
            loaded_path = std::move(m_async_loaded_path);
            loaded_ft = m_async_loaded_ft;
            loaded_error = std::move(m_async_loaded_error);
            m_file_load_pending.store(false);
        }

        if (loaded_error.empty()) {
            m_input_text = std::move(loaded_content);
            m_loaded_srt_cues = std::move(loaded_cues);
            snprintf(m_doc_path_buf, sizeof(m_doc_path_buf), "%s", loaded_path.c_str());

            if (loaded_ft == DocParser::FileType::SUBTITLE || loaded_ft == DocParser::FileType::LOCALIZATION) {
                m_preserve_code_keys = true;
            }
            if (loaded_ft == DocParser::FileType::SUBTITLE) {
                snprintf(m_export_path_buf, sizeof(m_export_path_buf), "%s", (loaded_path + ".translated.srt").c_str());
            }

            m_input_source_idx = 2;
            m_status_msg = "Loaded file into editor: " + loaded_path +
                (!m_loaded_srt_cues.empty() ? " (" + std::to_string(m_loaded_srt_cues.size()) + " subtitle cues locked)" : "");
            m_status_is_error = false;
        }
        else {
            m_status_msg = "Document error: " + loaded_error;
            m_status_is_error = true;
        }
    }

    bool output_updated = false;

    if (m_is_headless_epub_task) {
        std::string headless_stream_snapshot;
        if (m_engine->get_output_text_if_dirty(headless_stream_snapshot, m_output_revision)) {
            m_cached_output_chars = headless_stream_snapshot.size();
            m_cached_output_words = m_cached_output_chars / 6;

            size_t cur_c = 0, tot_c = 0, tm_h = 0;
            float pct = 0.0f;
            m_engine->get_chunk_progress(cur_c, tot_c, pct, tm_h);
            m_status_msg = "EPUB Continuous File Converter: File " + std::to_string(cur_c) + " / " + std::to_string(tot_c) +
                " (" + std::to_string(static_cast<int>(pct * 100.0f)) + "%) | " +
                std::to_string(m_cached_output_words) + " words converted...";
            m_status_is_error = false;
        }
    }
    else {
        output_updated = m_engine->get_output_text_if_dirty(m_output_text, m_output_revision);
    }

    bool is_generating = m_engine->is_generating();
    bool just_finished = m_was_generating && !is_generating;

    if (output_updated) {
        m_cached_output_chars = m_output_text.size();
        if (!is_generating || (now - m_last_word_count_time > 0.25)) {
            m_cached_output_words = count_words(m_output_text);
            m_last_word_count_time = now;
        }
    }

    if (just_finished) {
        if (m_last_task_mode == AITaskMode::Chat) {
            // Store both user question and AI answer in memory!
            m_chat_history.push_back({ "user", m_last_chat_input });
            m_chat_history.push_back({ "assistant", m_output_text });

            m_cached_output_words = count_words(m_output_text);
            m_cached_output_chars = m_output_text.size();
            m_status_msg = "AI replied. Ready for next message.";
            m_status_is_error = false;
        }
        else if (m_is_headless_epub_task) {
            std::string err_str = m_engine->get_last_error();
            if (err_str.empty()) {
                m_status_msg = "Successfully created converted EPUB: " + m_headless_epub_out_path;
                m_status_is_error = false;
            }
            else {
                m_status_msg = "EPUB Conversion Error: " + err_str;
                m_status_is_error = true;
            }
            m_is_headless_epub_task = false;
        }
        else {
            m_cached_output_words = count_words(m_output_text);
            m_cached_output_chars = m_output_text.size();
            if (m_show_inline_diff) rebuild_diff_tokens();
            m_status_msg = "Completed successfully. (" + std::to_string(m_cached_output_words) + " words generated)";
            m_status_is_error = false;
        }
    }

    m_was_generating = is_generating;

    if (m_input_text.size() != m_last_input_size) {
        m_cached_input_words = count_words(m_input_text);
        m_cached_input_chars = m_input_text.size();
        m_last_input_size = m_input_text.size();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("MainLayout", nullptr, window_flags);

    render_header_panel();
    ImGui::Separator();

    render_controls_panel();

    if (m_show_glossary_drawer) {
        render_glossary_drawer();
    }

    if (m_engine->is_generating()) {
        render_progress_and_cancel_bar();
    }

    ImGui::Separator();

    render_text_editors_panel();
    render_status_bar();

    ImGui::End();

    if (m_show_download_window) {
        render_download_window();
    }
    render_model_loading_overlay();
}

void AppUI::render_donation_button() {
    double t = ImGui::GetTime();
    float pulse = (static_cast<float>(std::sin(t * 5.0)) + 1.0f) * 0.5f;

    ImVec4 col_button = ImVec4(0.85f, 0.45f + 0.20f * pulse, 0.10f, 1.0f);
    ImVec4 col_button_hover = ImVec4(0.95f, 0.55f + 0.20f * pulse, 0.20f, 1.0f);
    ImVec4 col_button_active = ImVec4(0.75f, 0.35f, 0.05f, 1.0f);

    ImGui::SameLine();
    if (ImGui::Button(m_is_mini_mode ? "Expand UI##MiniToggle" : "Mini Overlay##MiniToggle")) {
        toggle_mini_mode();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, col_button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col_button_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col_button_active);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    ImGui::SameLine(ImGui::GetWindowWidth() - 190.0f);
    if (ImGui::Button(" Buy me a coffee ##DonationBtn", ImVec2(170, 30))) {
        open_url("https://buymeacoffee.com/www.nutz.club");
    }

    ImGui::PopStyleColor(4);
}

void AppUI::render_progress_and_cancel_bar() {
    ImGui::Spacing();

    double t = ImGui::GetTime();
    float pulse = (static_cast<float>(std::sin(t * 4.0)) + 1.0f) * 0.5f;

    size_t cur_chunk = 0, tot_chunks = 0, tm_hits = 0;
    float chunk_pct = 0.0f;
    m_engine->get_chunk_progress(cur_chunk, tot_chunks, chunk_pct, tm_hits);

    char progress_str[200];
    if (m_is_headless_epub_task) {
        snprintf(
            progress_str,
            sizeof(progress_str),
            "EPUB Continuous Converter: File %zu / %zu (%.1f%%) | %zu words converted",
            cur_chunk, tot_chunks, chunk_pct * 100.0f, m_cached_output_words
        );
    }
    else if (tot_chunks > 1) {
        snprintf(
            progress_str,
            sizeof(progress_str),
            "Chunk %zu/%zu (%.1f%%) | TM Cache Hits: %zu | %zu words (%zu chars)",
            cur_chunk, tot_chunks, chunk_pct * 100.0f, tm_hits, m_cached_output_words, m_cached_output_chars
        );
    }
    else {
        snprintf(
            progress_str,
            sizeof(progress_str),
            "Generating... (%zu words / %zu chars generated)",
            m_cached_output_words, m_cached_output_chars
        );
    }

    float avail_w = ImGui::GetContentRegionAvail().x - 170.0f;

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.60f + 0.30f * pulse, 0.95f, 1.0f));
    ImGui::ProgressBar(tot_chunks > 1 ? chunk_pct : -1.0f, ImVec2(avail_w, 32.0f), progress_str);
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 10.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.28f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.68f, 0.14f, 0.14f, 1.0f));

    if (ImGui::Button(" CANCEL / STOP ##ProgressBarCancel", ImVec2(150, 32))) {
        m_engine->abort();
        if (m_is_headless_epub_task) {
            EpubProcessor::cleanup_state(m_epub_state);
            m_is_headless_epub_task = false;
        }
        m_status_msg = "Generation cancelled by user.";
        m_status_is_error = true;
    }

    ImGui::PopStyleColor(3);
    ImGui::Spacing();
}

void AppUI::render_download_window() {
    ImGui::SetNextWindowSize(ImVec2(740, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AI Models Management##FloatingWindow", &m_show_download_window, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    const auto& presets = ModelDownloader::get_presets();
    int installed_count = count_installed_official_presets();

    ImGui::TextColored(ImVec4(0.24f, 0.72f, 1.0f, 1.0f), "Local AI Models & Downloads");
    ImGui::TextWrapped("Downloaded models automatically appear in your active model selection list.");
    ImGui::Separator();
    ImGui::Spacing();

    bool downloading = m_downloader.is_downloading();
    std::string active_dl_file = m_downloader.get_active_filename();

    for (size_t i = 0; i < presets.size(); ++i) {
        bool installed = is_file_present(presets[i].filename);
        bool is_this_downloading = downloading && (active_dl_file == presets[i].filename);

        ImGui::PushID(static_cast<int>(i));

        ImGui::Text("%s", presets[i].name.c_str());
        ImGui::TextDisabled("%s", presets[i].description.c_str());

        ImGui::SameLine(ImGui::GetWindowWidth() - 180.0f);

        if (installed) {
            ImGui::TextColored(ImVec4(0.24f, 0.85f, 0.40f, 1.0f), "[ Installed ]");
        }
        else if (is_this_downloading) {
            ImGui::BeginDisabled();
            ImGui::Button("Downloading...##PresetActive", ImVec2(150, 32));
            ImGui::EndDisabled();
        }
        else if (downloading) {
            ImGui::BeginDisabled();
            ImGui::Button("Wait Queue##PresetWait", ImVec2(150, 32));
            ImGui::EndDisabled();
        }
        else {
            if (ImGui::Button("Download Now##PresetDl", ImVec2(150, 32))) {
                m_downloader.start_download(presets[i]);
            }
        }

        ImGui::PopID();
        ImGui::Separator();
        ImGui::Spacing();
    }

    if (downloading) {
        ImGui::Text("Status: %s", m_downloader.get_status_message().c_str());
        ImGui::ProgressBar(m_downloader.get_progress(), ImVec2(-1.0f, 0.0f));
        if (m_downloader.is_cancel_requested()) {
            ImGui::BeginDisabled();
            ImGui::Button("Cancelling...##DlCancellingBtn");
            ImGui::EndDisabled();
        }
        else if (ImGui::Button("Cancel Active Download##DlCancelActiveBtn")) {
            m_downloader.cancel_download();
        }
    }
    else if (m_downloader.is_completed()) {
        scan_available_models();
        std::string downloaded_path = m_downloader.get_downloaded_filepath();
        for (size_t i = 0; i < m_available_models.size(); ++i) {
            if (fs::path(m_available_models[i]).filename() == fs::path(downloaded_path).filename()) {
                m_selected_model_idx = static_cast<int>(i);
                break;
            }
        }
        update_active_languages();
        m_downloader.reset_completed_flag();

        if (!downloaded_path.empty()) {
            m_engine->load_model_async(downloaded_path, static_cast<uint32_t>(m_ctx_size));
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Installed: %d / %zu official models", installed_count, presets.size());
    ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
    if (ImGui::Button("Close##CloseDlWindowBtn", ImVec2(120, 32))) {
        m_show_download_window = false;
    }

    ImGui::End();
}

void AppUI::render_header_panel() {
    ImGui::TextColored(ImVec4(0.25f, 0.75f, 1.0f, 1.0f), "Local AI Translation & Publishing Suite Pro");
    ImGui::SameLine();
    ImGui::TextDisabled("(SDL3 + ImGui + OpenGL3 + ");
    ImGui::SameLine(0.0f, 0.0f);

    double cur_time = ImGui::GetTime();
    float flash_t = (static_cast<float>(std::sin(cur_time * 5.0)) + 1.0f) * 0.5f;
    ImVec4 libinfer_col = ImVec4(
        0.20f + 0.70f * flash_t,
        0.65f + 0.35f * (1.0f - flash_t),
        1.00f,
        1.00f
    );

    ImGui::PushStyleColor(ImGuiCol_Text, libinfer_col);
    ImGui::TextUnformatted("libinfer");
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Visit libinfer on GitHub: https://github.com/codeboxqc/libinfer\n(Click to open link)");
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        open_url("https://github.com/codeboxqc/libinfer");
    }
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextDisabled(")");

    render_donation_button();

    bool busy = m_engine->is_loading_model() || m_engine->is_generating();
    if (busy) ImGui::BeginDisabled();

    ImGui::PushItemWidth(320.0f);
    if (m_available_models.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "No .gguf models found!");
    }
    else {
        std::vector<const char*> model_ptrs;
        model_ptrs.reserve(m_available_models.size());
        for (const auto& m : m_available_models) model_ptrs.push_back(m.c_str());

        int prev_idx = m_selected_model_idx;
        if (ImGui::Combo("Active Model##HeaderModelCombo", &m_selected_model_idx, model_ptrs.data(), static_cast<int>(model_ptrs.size()))) {
            if (m_selected_model_idx != prev_idx && !m_available_models.empty()) {
                if (m_engine->load_model_async(m_available_models[m_selected_model_idx], static_cast<uint32_t>(m_ctx_size))) {
                    m_in_lang_idx = 0;
                    m_out_lang_idx = 1;
                    update_active_languages();
                    m_status_msg = "Switching model to: " + m_available_models[m_selected_model_idx];
                    m_status_is_error = false;
                }
                else {
                    m_selected_model_idx = prev_idx;
                }
            }
        }
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::PushItemWidth(75.0f);
    if (ImGui::InputInt("Ctx (0=Auto)##HeaderCtxInput", &m_ctx_size, 0)) {
        if (m_ctx_size < 0) m_ctx_size = 0;
        else if (m_ctx_size > 0) m_ctx_size = std::clamp(m_ctx_size, 512, 131072);
    }
    ImGui::PopItemWidth();

    if (busy) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Models##HeaderOpenModelsBtn")) {
        m_show_download_window = !m_show_download_window;
    }

    ImGui::SameLine();
    if (ImGui::Button(m_show_glossary_drawer ? "Hide Glossary##HeaderGlossaryBtn" : "Glossary Lock##HeaderGlossaryBtn")) {
        m_show_glossary_drawer = !m_show_glossary_drawer;
    }

    ImGui::SameLine();
    if (m_engine->is_loading_model()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[LOADING MODEL...]");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Model weights are currently being mapped to memory and VRAM in the background...");
        }
    }
    else if (m_engine->is_model_loaded()) {
        bool is_gpu = m_engine->is_using_gpu();
        if (is_gpu) {
            ImGui::TextColored(ImVec4(0.24f, 0.72f, 1.0f, 1.0f), "[GPU Ready | %u Ctx]", m_engine->get_active_ctx());
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "[CPU Mode - SLOW PERFORMANCE | %u Ctx]", m_engine->get_active_ctx());
        }

        if (ImGui::IsItemHovered()) {
            std::string tip = m_engine->get_hardware_info() + "\n" + m_engine->get_memory_info() +
                "\nActive: " + m_engine->get_loaded_model_path();
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    else {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "[No Model]");
    }
}

void AppUI::render_glossary_drawer() {
    ImGui::BeginChild("GlossaryDrawer##Panel", ImVec2(0, 105), true);
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.65f, 1.0f), "Custom Terminology & Glossary Lock (Format: Source Term = Target Term, one per line):");
    ImGui::SameLine();
    if (ImGui::SmallButton("Import Output into Glossary##DrawerImportBtn")) {
        if (!m_output_text.empty()) {
            if (!m_glossary_text.empty() && m_glossary_text.back() != '\n') m_glossary_text += "\n";
            m_glossary_text += m_output_text;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear Glossary##DrawerClearBtn")) {
        m_glossary_text.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Purge Disk TM Cache##DrawerPurgeTmBtn")) {
        m_engine->clear_tm_cache();
        m_status_msg = "Translation Memory (TM) disk cache cleared.";
        m_status_is_error = false;
    }

    InputTextMultilineStdString("##GlossaryInput", &m_glossary_text, ImVec2(-FLT_MIN, -FLT_MIN));
    ImGui::EndChild();
}

void AppUI::render_controls_panel() {
    const char* sources[] = { "Window Text", "Clipboard", "Loaded File (.txt/.docx/.epub/.srt)" };

    ImGui::PushItemWidth(210.0f);
    ImGui::Combo("Source##ControlsSourceCombo", &m_input_source_idx, sources, IM_ARRAYSIZE(sources));
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 12.0f);

    ImGui::PushItemWidth(170.0f);
    ImGui::Combo("In Lang##ControlsInLangCombo", &m_in_lang_idx, m_active_lang_ptrs.data(), static_cast<int>(m_active_lang_ptrs.size()));
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 8.0f);
    if (ImGui::Button("<->##ControlsSwapLangBtn")) {
        std::swap(m_in_lang_idx, m_out_lang_idx);
    }
    ImGui::SameLine(0, 8.0f);

    ImGui::PushItemWidth(170.0f);
    ImGui::Combo("Out Lang##ControlsOutLangCombo", &m_out_lang_idx, m_active_lang_ptrs.data(), static_cast<int>(m_active_lang_ptrs.size()));
    ImGui::PopItemWidth();

    if (m_input_source_idx == 2) {
        ImGui::SameLine(0, 12.0f);
        ImGui::PushItemWidth(180.0f);
        ImGui::InputText("Doc Path##ControlsDocPathInput", m_doc_path_buf, sizeof(m_doc_path_buf));
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Load File##ControlsLoadFileBtn")) {
            handle_file_drop(m_doc_path_buf);
        }
    }

    ImGui::Spacing();
    float avail_width = ImGui::GetContentRegionAvail().x;
    float buttons_width = 450.0f;
    float input_width = std::max(200.0f, avail_width - buttons_width);

    ImGui::PushItemWidth(input_width);
    InputTextWithHintStdString(
        "##CustomDirective",
        "AI Directive (e.g. 'Use formal tone', 'Keep code blocks verbatim')...",
        &m_custom_directive
    );
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 6.0f);
    if (ImGui::Button("Formal##PresetFormalBtn", ImVec2(60, 0))) {
        m_custom_directive = "Use a formal, polite, and professional tone throughout.";
    }
    ImGui::SameLine(0, 4.0f);
    if (ImGui::Button("Casual##PresetCasualBtn", ImVec2(60, 0))) {
        m_custom_directive = "Use a natural, conversational, and casual tone.";
    }
    ImGui::SameLine(0, 4.0f);
    if (ImGui::Button("Technical##PresetTechnicalBtn", ImVec2(75, 0))) {
        m_custom_directive = "Preserve all technical terminology, acronyms, and code identifiers verbatim.";
    }
    ImGui::SameLine(0, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.22f, 0.68f, 1.0f));
    if (ImGui::Button("Book Layout##PresetBookLayoutBtn", ImVec2(95, 0))) {
        apply_layout_directive();
        m_layout_formatting = true;
    }
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0, 4.0f);
    if (ImGui::Button("Clear##DirectiveClearBtn", ImVec2(50, 0))) {
        m_custom_directive.clear();
    }

    ImGui::Checkbox("Fix Typos##ToggleFixTypos", &m_auto_correct_on_translate);
    ImGui::SameLine(0, 14.0f);
    ImGui::Checkbox("Book Layout##ToggleBookLayout", &m_layout_formatting);
    ImGui::SameLine(0, 14.0f);
    ImGui::Checkbox("4-Msg Continuity##ToggleContinuity", &m_enable_continuity);
    ImGui::SameLine(0, 14.0f);
    ImGui::Checkbox("TM Disk Cache##ToggleTmCache", &m_enable_tm_cache);
    ImGui::SameLine(0, 14.0f);
    ImGui::Checkbox("Lock Keys/SRT##ToggleLockKeys", &m_preserve_code_keys);
    ImGui::SameLine(0, 14.0f);
    if (ImGui::Checkbox("Inline Diff View##ToggleInlineDiff", &m_show_inline_diff)) {
        if (m_show_inline_diff) rebuild_diff_tokens();
    }
    if (m_engine->is_qwen3_loaded()) {
        ImGui::SameLine(0, 14.0f);
        ImGui::Checkbox("Qwen3 Deep Think (/think)##ToggleQ3DeepThink", &m_qwen3_deep_think);
    }

    ImGui::Spacing();

    bool generating = m_engine->is_generating();
    bool loading_model = m_engine->is_loading_model();

    if (generating) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button(" STOP / CANCEL ##ControlsStopCancelBtn", ImVec2(150, 34))) {
            m_engine->abort();
            if (m_is_headless_epub_task) {
                EpubProcessor::cleanup_state(m_epub_state);
                m_is_headless_epub_task = false;
            }
            m_status_msg = "Abort requested.";
            m_status_is_error = true;
        }
        ImGui::PopStyleColor(1);
    }
   
     

    else {
        // Disable action buttons while model is loading or not loaded
        bool disable_actions = !m_engine->is_model_loaded() || loading_model;
        if (disable_actions) ImGui::BeginDisabled();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.40f, 1.0f));
        if (ImGui::Button(" TRANSLATE ##ActionTranslateBtn", ImVec2(115, 34))) {
            dispatch_ai_task(AITaskMode::Translate, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.48f, 0.82f, 1.0f));
        if (ImGui::Button(" FIX TYPOS ##ActionFixTyposBtn", ImVec2(105, 34))) {
            dispatch_ai_task(AITaskMode::AutoCorrect, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.60f, 0.35f, 1.0f));
        if (ImGui::Button(" CHAT ##ActionChatBtn", ImVec2(90, 34))) {
            if (!m_input_text.empty()) {
                m_last_chat_input = m_input_text;
                dispatch_ai_task(AITaskMode::Chat, false);
                m_input_text.clear();
            }
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.65f, 1.0f));
        if (ImGui::Button(" CODE ##ActionCodeBtn", ImVec2(115, 34))) {
            snprintf(m_export_path_buf, sizeof(m_export_path_buf), "solution.cpp");
            dispatch_ai_task(AITaskMode::CodeAssistant, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.44f, 0.24f, 0.72f, 1.0f));
        if (ImGui::Button(" LAYOUT ##ActionLayoutBtn", ImVec2(85, 34))) {
            apply_layout_directive();
            m_layout_formatting = true;
            dispatch_ai_task(AITaskMode::FormatLayout, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.52f, 0.62f, 1.0f));
        if (ImGui::Button(" BILINGUAL ##ActionBilingualBtn", ImVec2(100, 34))) {
            dispatch_ai_task(AITaskMode::BilingualAlign, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.42f, 0.14f, 1.0f));
        if (ImGui::Button(" SUMMARIZE ##ActionSummarizeBtn", ImVec2(105, 34))) {
            dispatch_ai_task(AITaskMode::ExecutiveSummary, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.58f, 0.22f, 0.45f, 1.0f));
        if (ImGui::Button(" TRANSCREATE ##ActionTranscreateBtn", ImVec2(115, 34))) {
            dispatch_ai_task(AITaskMode::Transcreate, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.45f, 0.35f, 1.0f));
        if (ImGui::Button(" GLOSSARY AI ##ActionGlossaryBtn", ImVec2(110, 34))) {
            m_show_glossary_drawer = true;
            dispatch_ai_task(AITaskMode::ExtractGlossary, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.26f, 0.36f, 1.0f));
        if (ImGui::Button("Retry Mistake##ActionRetryBtn", ImVec2(110, 34))) {
            dispatch_ai_task(m_last_task_mode, true);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.32f, 0.12f, 1.0f));
        if (ImGui::Button(" EPUB File ##ActionEpubFormatBtn", ImVec2(125, 34))) {
            start_epub_format_translation();
        }
        ImGui::PopStyleColor(1);

        // Re-enable UI
        if (disable_actions) ImGui::EndDisabled();
    }

    ImGui::SameLine(0, 6.0f);

    if (ImGui::Button("Clear All##EditorsClearAllBtn", ImVec2(80, 34))) {
        m_chat_history.clear();
        m_last_chat_input.clear();
        m_input_text.clear();
        m_input_text.shrink_to_fit();

        m_engine->clear_output_text();

        m_output_text.clear();
        m_output_text.shrink_to_fit();

        m_diff_tokens.clear();
        m_diff_tokens.shrink_to_fit();

        m_loaded_srt_cues.clear();
        m_loaded_srt_cues.shrink_to_fit();

        m_cached_input_words = 0;
        m_cached_input_chars = 0;
        m_cached_output_words = 0;
        m_cached_output_chars = 0;
        m_last_input_size = 0;
        m_status_msg = "Cleared editors and reclaimed heap memory.";
        m_status_is_error = false;
    }
}

void AppUI::render_inline_diff_view() {
    if (m_diff_computed_revision != m_output_revision && !m_engine->is_generating()) {
        rebuild_diff_tokens();
    }

    ImGui::BeginChild("DiffScrollView##ViewChild", ImVec2(-FLT_MIN, -FLT_MIN), false, ImGuiWindowFlags_HorizontalScrollbar);
    float wrap_width = ImGui::GetContentRegionAvail().x - 20.0f;
    float cur_line_w = 0.0f;

    for (const auto& tok : m_diff_tokens) {
        ImVec4 col = ImVec4(0.90f, 0.92f, 0.96f, 1.0f);
        std::string label = tok.text;
        if (tok.type == DiffToken::Removed) {
            col = ImVec4(0.98f, 0.38f, 0.38f, 1.0f);
            label = "[-" + tok.text + "]";
        }
        else if (tok.type == DiffToken::Added) {
            col = ImVec4(0.30f, 0.92f, 0.52f, 1.0f);
            label = "[+" + tok.text + "]";
        }

        float tok_w = ImGui::CalcTextSize(label.c_str()).x + 8.0f;
        if (cur_line_w + tok_w > wrap_width && cur_line_w > 0.0f) {
            cur_line_w = 0.0f;
        }
        else if (cur_line_w > 0.0f) {
            ImGui::SameLine(0.0f, 5.0f);
        }

        ImGui::TextColored(col, "%s", label.c_str());
        cur_line_w += tok_w;
    }
    ImGui::EndChild();
}

void AppUI::render_text_editors_panel() {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float avail_height = ImGui::GetContentRegionAvail().y - 35.0f;
    float box_width = (avail_width - 15.0f) * 0.5f;

    ImGui::BeginChild("InputGroup##InputChild", ImVec2(box_width, avail_height), true);
    ImGui::TextUnformatted("Source Input Text:");

    ImGui::SameLine();
    if (ImGui::SmallButton("Paste Clipboard##PasteSourceClipBtn")) {
        char* sdl_clip = SDL_GetClipboardText();
        if (sdl_clip) {
            if (std::strlen(sdl_clip) > 0) {
                m_input_text = sdl_clip;
            }
            SDL_free(sdl_clip);
        }
    }


    ImGui::SameLine();
    if (ImGui::SmallButton("Export HTML##SaveHtmlFileBtn")) {
        std::string err;
        std::string html_path = std::string(m_export_path_buf) + ".html";
        if (DocParser::save_html_document(html_path, "Translation Document", m_output_text, err)) {
            m_status_msg = "Exported print-ready HTML to " + html_path;
            m_status_is_error = false;
        }
        else {
            m_status_msg = err;
            m_status_is_error = true;
        }
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 185.0f);
    ImGui::TextDisabled("Words: %zu | Chars: %zu", m_cached_input_words, m_cached_input_chars);

    InputTextMultilineStdString("##SourceText", &m_input_text, ImVec2(-FLT_MIN, -FLT_MIN));
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("OutputGroup##OutputChild", ImVec2(box_width, avail_height), true);
    ImGui::TextUnformatted("Output:");
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll##ToggleAutoScroll", &m_auto_scroll);

    ImGui::SameLine();
    // Copy Output Button:
    if (ImGui::SmallButton("Copy##CopyOutputTextBtn")) {
        if (!m_output_text.empty()) {
            SDL_SetClipboardText(m_output_text.c_str());
            m_status_msg = "Copied output to system clipboard!";
            m_status_is_error = false;
        }
    }
    

    ImGui::SameLine();
    ImGui::PushItemWidth(135.0f);
    ImGui::InputText("##ExportPath", m_export_path_buf, sizeof(m_export_path_buf));
    ImGui::PopItemWidth();
    ImGui::SameLine();

    if (ImGui::SmallButton("Save/Export##SaveExportFileBtn")) {
        std::string err;
        if (DocParser::save_document(m_export_path_buf, m_output_text, m_loaded_srt_cues, err)) {
            m_status_msg = std::string("Exported output to ") + m_export_path_buf;
            m_status_is_error = false;
        }
        else {
            m_status_msg = err;
            m_status_is_error = true;
        }
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 185.0f);
    ImGui::TextDisabled("Words: %zu | Chars: %zu", m_cached_output_words, m_cached_output_chars);

    if (m_show_inline_diff) {
        render_inline_diff_view();
    }
    else {
        InputTextMultilineStdString(
            "##OutputText",
            &m_output_text,
            ImVec2(-FLT_MIN, -FLT_MIN),
            ImGuiInputTextFlags_ReadOnly
        );
        if (m_auto_scroll && m_engine->is_generating()) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

void AppUI::render_status_bar() {
    ImGui::Separator();
    if (m_engine->is_loading_model()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[LOADING MODEL IN BACKGROUND...]");
        ImGui::SameLine();
    }
    else if (m_engine->is_generating()) {
        ImGui::TextColored(ImVec4(0.24f, 0.72f, 1.0f, 1.0f), "[PROCESSING...]");
        ImGui::SameLine();
    }

    if (m_status_is_error) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Error: %s", m_status_msg.c_str());
    }
    else {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Status: %s", m_status_msg.c_str());
    }

    if (m_engine->is_generating()) {
        float tps = m_engine->get_live_tps();
        ImGui::SameLine(0, 15.0f);
        ImGui::TextColored(ImVec4(0.20f, 0.85f, 0.40f, 1.0f), "[Speed: %.1f tokens/sec]", tps);
    }


}



void AppUI::apply_dynamic_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    bool model_loaded = m_engine->is_model_loaded();
    bool is_cpu = model_loaded && !m_engine->is_using_gpu();

    if (is_cpu) {
        // --- RED THEME FOR CPU SLOW MODE ---
        colors[ImGuiCol_WindowBg] = ImVec4(0.14f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.18f, 0.07f, 0.07f, 0.85f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.18f, 0.07f, 0.07f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.65f, 0.18f, 0.18f, 0.70f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.26f, 0.09f, 0.09f, 0.80f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.36f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.46f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.18f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.48f, 0.12f, 0.12f, 0.90f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.62f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.72f, 0.22f, 0.22f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.42f, 0.12f, 0.12f, 0.80f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.58f, 0.18f, 0.18f, 0.90f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.92f, 0.22f, 0.22f, 1.00f);
    }
    else {
        // --- NORMAL BLUE DARK THEME FOR GPU MODE ---
        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.12f, 0.16f, 0.85f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.12f, 0.16f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.18f, 0.22f, 0.30f, 0.70f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.16f, 0.22f, 0.80f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.23f, 0.32f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.28f, 0.38f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.14f, 0.19f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.15f, 0.20f, 0.28f, 0.90f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.29f, 0.40f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.37f, 0.52f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.15f, 0.21f, 0.30f, 0.80f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.30f, 0.44f, 0.90f);
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.20f, 0.65f, 0.95f, 1.00f);
    }
}

void AppUI::render_model_loading_overlay() {
    if (!m_engine->is_loading_model()) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs;

    ImGui::Begin("##ModelLoadingWaitOverlay", nullptr, flags);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 v_min = viewport->Pos;
    ImVec2 v_max = ImVec2(v_min.x + viewport->Size.x, v_min.y + viewport->Size.y);

    // Dark background dim
    draw_list->AddRectFilled(v_min, v_max, IM_COL32(5, 7, 10, 220));

    // Centered Spinner
    ImVec2 center = ImVec2(v_min.x + viewport->Size.x * 0.5f, v_min.y + viewport->Size.y * 0.5f);
    float t = static_cast<float>(ImGui::GetTime());
    float radius = 35.0f;
    int num_segments = 36;
    float start_angle = t * 7.0f;
    float end_angle = start_angle + 4.8f;

    draw_list->PathClear();
    for (int i = 0; i <= num_segments; i++) {
        float a = start_angle + (static_cast<float>(i) / num_segments) * (end_angle - start_angle);
        draw_list->PathLineTo(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
    }
    draw_list->PathStroke(IM_COL32(255, 180, 40, 255), 0, 6.0f);

    // Text Label
    const char* msg = "Loading AI Model Weights... Please Wait";
    ImVec2 text_size = ImGui::CalcTextSize(msg);
    ImGui::SetCursorScreenPos(ImVec2(center.x - text_size.x * 0.5f, center.y + radius + 25.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", msg);

    ImGui::End();
}



void AppUI::toggle_mini_mode() {
    if (!m_sdl_window) return;
    m_is_mini_mode = !m_is_mini_mode;
    SDL_SetWindowAlwaysOnTop(m_sdl_window, m_is_mini_mode ? true : false);
    if (m_is_mini_mode) {
        SDL_SetWindowSize(m_sdl_window, 460, 320); // Compact mini float mode
    }
    else {
        SDL_SetWindowSize(m_sdl_window, 1420, 880); // Restores full suite
    }
}