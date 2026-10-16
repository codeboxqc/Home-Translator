/* ========================================================================== */
/* app_ui.cpp                                                                 */
/* Clean, Popup-Free, 100% Responsive UI Layout                               */
/* ========================================================================== */
#include "app_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

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
        std::istringstream iss(text);
        std::string w;
        while (iss >> w && tokens.size() < max_words) {
            tokens.push_back(w);
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
        // Start loading the first available model silently in the background
        m_engine->load_model_async(m_available_models[0], static_cast<uint32_t>(m_ctx_size));
        m_show_download_window = false;
        m_status_msg = (installed_presets < 3)
            ? "Model initialized in background. Additional presets available in 'Models'."
            : "All models present. Ready.";
        m_status_is_error = false;
    }
}

AppUI::~AppUI() {}

void AppUI::apply_layout_directive() {
    m_custom_directive =
        "Apply professional book and text layout formatting:\n"
        "1. Structure into logical, readable paragraphs.\n"
        "2. Ensure proper sentence-start capitalization.\n"
        "3. Standardize punctuation (periods, commas, em-dashes for dialogs).\n"
        "4. Preserve original story, meaning, and voice 100% faithfully.";
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

    std::string path(dropped_filepath);
    m_status_msg = "Loading document... (Please wait)";
    m_status_is_error = false;

    std::thread([this, path]() {
        std::string error, loaded_content;
        std::vector<SubtitleCue> cues;
        bool success = DocParser::load_document(path, loaded_content, error, &cues);
        DocParser::FileType ft = DocParser::detect_file_type(path);

        std::lock_guard<std::mutex> lock(m_file_load_mutex);
        m_async_loaded_content = std::move(loaded_content);
        m_async_loaded_cues = std::move(cues);
        m_async_loaded_path = path;
        m_async_loaded_ft = ft;
        m_async_loaded_error = std::move(error);
        m_file_load_pending.store(true);
    }).detach();
}

void AppUI::trigger_hotkey_clipboard_translate() {
    if (m_engine->is_generating() || m_engine->is_loading_model()) return;
    const char* clip = ImGui::GetClipboardText();
    if (clip && std::strlen(clip) > 0) {
        m_input_text = clip;
        m_input_source_idx = 1;
        dispatch_ai_task(AITaskMode::Translate, false);
    }
}

void AppUI::dispatch_ai_task(AITaskMode mode, bool is_retry) {
    if (!is_retry && m_input_source_idx == 1) {
        const char* clipboard_text = ImGui::GetClipboardText();
        if (clipboard_text && std::strlen(clipboard_text) > 0) {
            m_input_text = clipboard_text;
        }
        else {
            m_status_msg = "Clipboard is empty!";
            m_status_is_error = true;
            return;
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

    m_last_task_mode = mode;

    // Clear the output string here so that delta updates don't keep appending to previous session
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
    double now = ImGui::GetTime();

    if (m_file_load_pending.load()) {
        std::lock_guard<std::mutex> lock(m_file_load_mutex);

        if (m_async_loaded_error.empty()) {
            m_input_text = std::move(m_async_loaded_content);
            m_loaded_srt_cues = std::move(m_async_loaded_cues);
            snprintf(m_doc_path_buf, sizeof(m_doc_path_buf), "%s", m_async_loaded_path.c_str());

            if (m_async_loaded_ft == DocParser::FileType::SUBTITLE || m_async_loaded_ft == DocParser::FileType::LOCALIZATION) {
                m_preserve_code_keys = true;
            }
            if (m_async_loaded_ft == DocParser::FileType::SUBTITLE) {
                snprintf(m_export_path_buf, sizeof(m_export_path_buf), "%s", (m_async_loaded_path + ".translated.srt").c_str());
            }

            m_input_source_idx = 2;
            m_status_msg = "Loaded file into editor: " + m_async_loaded_path +
                (!m_loaded_srt_cues.empty() ? " (" + std::to_string(m_loaded_srt_cues.size()) + " subtitle cues locked)" : "");
            m_status_is_error = false;
        } else {
            m_status_msg = "Document error: " + m_async_loaded_error;
            m_status_is_error = true;
        }

        m_file_load_pending.store(false);
    }

    bool output_updated = m_engine->get_output_text_if_dirty(m_output_text, m_output_revision);

    if (output_updated) {
        m_cached_output_chars = m_output_text.size();
        if (!m_engine->is_generating() || (now - m_last_word_count_time > 0.25)) {
            m_cached_output_words = count_words(m_output_text);
            m_last_word_count_time = now;
        }
    }
    else if (!m_engine->is_generating() && m_was_generating) {
        m_cached_output_words = count_words(m_output_text);
        m_cached_output_chars = m_output_text.size();
        if (m_show_inline_diff) rebuild_diff_tokens();
        m_status_msg = "Completed successfully. (" + std::to_string(m_cached_output_words) + " words generated)";
        m_status_is_error = false;
    }
    m_was_generating = m_engine->is_generating();

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

    // RENDER NON-BLOCKING DOWNLOAD WINDOW IF REQUESTED
    if (m_show_download_window) {
        render_download_window();
    }
}

void AppUI::render_donation_button() {
    double t = ImGui::GetTime();
    float pulse = (static_cast<float>(std::sin(t * 5.0)) + 1.0f) * 0.5f;

    ImVec4 col_button = ImVec4(0.85f, 0.45f + 0.20f * pulse, 0.10f, 1.0f);
    ImVec4 col_button_hover = ImVec4(0.95f, 0.55f + 0.20f * pulse, 0.20f, 1.0f);
    ImVec4 col_button_active = ImVec4(0.75f, 0.35f, 0.05f, 1.0f);

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

    char progress_str[160];
    if (tot_chunks > 1) {
        snprintf(progress_str, sizeof(progress_str), "Chunk %zu/%zu (%.1f%%) | TM Cache Hits: %zu | %zu words",
            cur_chunk, tot_chunks, chunk_pct * 100.0f, tm_hits, m_cached_output_words);
    }
    else {
        snprintf(progress_str, sizeof(progress_str), "Generating... (%zu words / %zu chars generated)",
            m_cached_output_words, m_cached_output_chars);
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
                // Silently load selected model in the background without opening any blocking popups!
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

    // INLINE GPU/CPU HARDWARE STATUS - WITH HOVER TOOLTIP (Zero popups, zero freezes!)
    ImGui::SameLine();
    if (m_engine->is_loading_model()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[LOADING MODEL...]");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Model weights are currently being mapped to memory and VRAM in the background...");
        }
    }
    else if (m_engine->is_model_loaded()) {
        bool is_gpu = m_engine->is_using_gpu();
        ImGui::TextColored(
            is_gpu ? ImVec4(0.24f, 0.72f, 1.0f, 1.0f) : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
            "[%s | %u Ctx]",
            is_gpu ? "GPU Ready" : "CPU Slow",
            m_engine->get_active_ctx()
        );
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

    // DIRECTIVE / INSTRUCTIONS
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

    // PRO ENGINE TOGGLES ROW
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

    // ACTION BUTTONS ROW
    bool generating = m_engine->is_generating();
    bool loading_model = m_engine->is_loading_model();

    if (generating) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button(" STOP / CANCEL ##ControlsStopCancelBtn", ImVec2(150, 34))) {
            m_engine->abort();
            m_status_msg = "Abort requested.";
            m_status_is_error = true;
        }
        ImGui::PopStyleColor(1);
    }
    else {
        if (!m_engine->is_model_loaded() || loading_model) ImGui::BeginDisabled();

        // 1. TRANSLATE
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.40f, 1.0f));
        if (ImGui::Button(" TRANSLATE ##ActionTranslateBtn", ImVec2(115, 34))) {
            dispatch_ai_task(AITaskMode::Translate, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        // 2. AUTO-CORRECT TYPOS
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.48f, 0.82f, 1.0f));
        if (ImGui::Button(" FIX TYPOS ##ActionFixTyposBtn", ImVec2(105, 34))) {
            dispatch_ai_task(AITaskMode::AutoCorrect, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        // 3. FORMAT LAYOUT
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.44f, 0.24f, 0.72f, 1.0f));
        if (ImGui::Button(" LAYOUT ##ActionLayoutBtn", ImVec2(85, 34))) {
            apply_layout_directive();
            m_layout_formatting = true;
            dispatch_ai_task(AITaskMode::FormatLayout, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        // 4. BILINGUAL ALIGN
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.52f, 0.62f, 1.0f));
        if (ImGui::Button(" BILINGUAL ##ActionBilingualBtn", ImVec2(100, 34))) {
            dispatch_ai_task(AITaskMode::BilingualAlign, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        // 5. EXECUTIVE SUMMARY
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.42f, 0.14f, 1.0f));
        if (ImGui::Button(" SUMMARIZE ##ActionSummarizeBtn", ImVec2(105, 34))) {
            dispatch_ai_task(AITaskMode::ExecutiveSummary, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        // 6. TRANSCREATE
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.58f, 0.22f, 0.45f, 1.0f));
        if (ImGui::Button(" TRANSCREATE ##ActionTranscreateBtn", ImVec2(115, 34))) {
            dispatch_ai_task(AITaskMode::Transcreate, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        // 7. EXTRACT GLOSSARY
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.45f, 0.35f, 1.0f));
        if (ImGui::Button(" GLOSSARY AI ##ActionGlossaryBtn", ImVec2(110, 34))) {
            m_show_glossary_drawer = true;
            dispatch_ai_task(AITaskMode::ExtractGlossary, false);
        }
        ImGui::PopStyleColor(1);

        ImGui::SameLine(0, 6.0f);

        // 8. RETRY MISTAKE
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.26f, 0.36f, 1.0f));
        if (ImGui::Button("Retry Mistake##ActionRetryBtn", ImVec2(110, 34))) {
            dispatch_ai_task(m_last_task_mode, true);
        }
        ImGui::PopStyleColor(1);

        if (!m_engine->is_model_loaded() || loading_model) ImGui::EndDisabled();
    }

    ImGui::SameLine(0, 6.0f);
    if (ImGui::Button("Clear All##EditorsClearAllBtn", ImVec2(80, 34))) {
        m_input_text.clear();
        if (m_input_text.capacity() > 1048576) m_input_text.shrink_to_fit();
        m_engine->clear_output_text();
        m_output_text.clear();
        if (m_output_text.capacity() > 1048576) m_output_text.shrink_to_fit();
        m_diff_tokens.clear();
        m_loaded_srt_cues.clear();
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
        const char* clip = ImGui::GetClipboardText();
        if (clip) m_input_text = clip;
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
    if (ImGui::SmallButton("Copy##CopyOutputTextBtn")) {
        ImGui::SetClipboardText(m_output_text.c_str());
        m_status_msg = "Copied output to system clipboard!";
        m_status_is_error = false;
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
        // Crash-proof multiline text widget with resize callback
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
}