/* ========================================================================== */
/* app_ui.cpp                                                                 */
/* Full UI implementation with Auto-Corrector & Chunk Progress Bar            */
/* ========================================================================== */
#include "app_ui.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
    struct InputTextCallback_UserData {
        std::string* Str;
        ImGuiInputTextCallback  ChainCallback;
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
        InputTextCallback_UserData cb_user_data;
        cb_user_data.Str = str;
        cb_user_data.ChainCallback = nullptr;
        cb_user_data.ChainCallbackUserData = nullptr;
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
        InputTextCallback_UserData cb_user_data;
        cb_user_data.Str = str;
        cb_user_data.ChainCallback = nullptr;
        cb_user_data.ChainCallbackUserData = nullptr;
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
#if defined(_WIN32)
        std::string cmd = "start \"\" \"" + url + "\"";
        std::system(cmd.c_str());
#elif defined(__APPLE__)
        std::string cmd = "open \"" + url + "\"";
        std::system(cmd.c_str());
#else
        std::string cmd = "xdg-open \"" + url + "\" &";
        std::system(cmd.c_str());
#endif
    }
} // anonymous namespace

AppUI::AppUI(AIEngine* engine) : m_engine(engine) {
    m_input_text.reserve(16384);
    m_output_text.reserve(16384);
    scan_available_models();
    update_active_languages();

    int installed_presets = count_installed_official_presets();

    if (installed_presets < 3) {
        m_show_download_modal = true;
        m_status_msg = "Startup Check: Missing AI model(s). Select models to download or click continue.";
        m_status_is_error = false;
    }
    else {
        m_show_download_modal = false;
        m_status_msg = "All models present. Auto-loading default model...";
        m_status_is_error = false;
    }

    if (!m_available_models.empty()) {
        m_engine->load_model_async(m_available_models[0], static_cast<uint32_t>(m_ctx_size));
    }
}

AppUI::~AppUI() {}

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
        "Khmer", "Lao", "Burmese", "Cebuano"
    };

    static const std::vector<std::string> qwen3_langs = {
        "English", "French (Français)", "Spanish (Espagnol)", "German (Allemand)",
        "Italian (Italien)", "Portuguese (Portugais)", "Dutch (Néerlandais)", "Russian (Russe)",
        "Polish (Polonais)", "Czech (Tchèque)", "Arabic (Arabe)", "Turkish (Turc)",
        "Persian (Farsi)", "Hebrew (Hébreu)", "Chinese (Chinois)", "Japanese (Japonais)",
        "Korean (Coréen)", "Hindi", "Bengali", "Urdu", "Vietnamese (Vietnamien)",
        "Thai (Thaï)", "Indonesian (Indonésien)", "Malay (Malais)", "Tagalog (Filipino)",
        "Khmer", "Lao", "Burmese", "Cebuano", "Swedish (Suédois)", "Danish (Danois)",
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

void AppUI::handle_file_drop(const char* dropped_filepath) {
    if (!dropped_filepath) return;

    std::string path(dropped_filepath);
    std::string error, loaded_content;

    if (DocParser::load_document(path, loaded_content, error)) {
        m_input_text = loaded_content;
        snprintf(m_doc_path_buf, sizeof(m_doc_path_buf), "%s", path.c_str());
        m_input_source_idx = 2;
        m_status_msg = "Loaded file into editor: " + path;
        m_status_is_error = false;
    }
    else {
        m_status_msg = "Document error: " + error;
        m_status_is_error = true;
    }
}

void AppUI::render() {
    if (m_engine->get_output_text_if_dirty(m_output_text, m_output_revision)) {
        m_cached_output_words = count_words(m_output_text);
        m_cached_output_chars = m_output_text.size();
    }

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

    if (m_engine->is_generating()) {
        render_progress_and_cancel_bar();
    }

    ImGui::Separator();

    render_text_editors_panel();
    render_status_bar();

    if (m_show_download_modal) {
        render_download_modal();
    }

    ImGui::End();
}

void AppUI::render_donation_button() {
    double t = ImGui::GetTime();
    float pulse = (static_cast<float>(std::sin(t * 6.0)) + 1.0f) * 0.5f;

    ImVec4 col_button = ImVec4(1.0f, 0.4f + 0.25f * pulse, 0.0f, 1.0f);
    ImVec4 col_button_hover = ImVec4(1.0f, 0.6f + 0.25f * pulse, 0.2f, 1.0f);
    ImVec4 col_button_active = ImVec4(0.85f, 0.3f, 0.0f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, col_button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col_button_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col_button_active);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    ImGui::SameLine(ImGui::GetWindowWidth() - 190.0f);
    if (ImGui::Button(" Buy me a coffee ", ImVec2(170, 30))) {
        open_url("https://buymeacoffee.com/www.nutz.club");
    }

    ImGui::PopStyleColor(4);
}

void AppUI::render_progress_and_cancel_bar() {
    ImGui::Spacing();

    double t = ImGui::GetTime();
    float pulse = (static_cast<float>(std::sin(t * 4.0)) + 1.0f) * 0.5f;

    size_t cur_chunk = 0, tot_chunks = 0;
    float chunk_pct = 0.0f;
    m_engine->get_chunk_progress(cur_chunk, tot_chunks, chunk_pct);

    char progress_str[128];
    if (tot_chunks > 1) {
        snprintf(progress_str, sizeof(progress_str), "Chunk %zu/%zu (%.1f%%) - %zu words generated",
            cur_chunk, tot_chunks, chunk_pct * 100.0f, m_cached_output_words);
    }
    else {
        snprintf(progress_str, sizeof(progress_str), "Generating... (%zu words / %zu chars generated)",
            m_cached_output_words, m_cached_output_chars);
    }

    float avail_w = ImGui::GetContentRegionAvail().x - 170.0f;

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f + 0.3f * pulse, 0.9f, 1.0f));
    ImGui::ProgressBar(tot_chunks > 1 ? chunk_pct : -1.0f, ImVec2(avail_w, 32.0f), progress_str);
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 10.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.28f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.12f, 0.12f, 1.0f));

    if (ImGui::Button(" CANCEL / STOP ", ImVec2(150, 32))) {
        m_engine->abort();
        m_status_msg = "Generation cancelled by user.";
        m_status_is_error = true;
    }

    ImGui::PopStyleColor(3);
    ImGui::Spacing();
}

void AppUI::render_download_modal() {
    ImGui::OpenPopup("AI Models Download Menu");
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("AI Models Download Menu", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& presets = ModelDownloader::get_presets();
        int installed_count = count_installed_official_presets();

        ImGui::TextColored(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), "AI Model Management & Download Menu");
        ImGui::TextWrapped("Download missing models below. When downloaded, models will auto-load automatically.");
        ImGui::Separator();
        ImGui::Spacing();

        bool downloading = m_downloader.is_downloading();

        for (size_t i = 0; i < presets.size(); ++i) {
            bool installed = is_file_present(presets[i].filename);

            ImGui::PushID(static_cast<int>(i));

            ImGui::Text("%s", presets[i].name.c_str());
            ImGui::TextDisabled("%s", presets[i].description.c_str());

            ImGui::SameLine(ImGui::GetWindowWidth() - 180.0f);

            if (installed) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[ Installed ]");
            }
            else if (downloading) {
                ImGui::BeginDisabled();
                ImGui::Button("Downloading...", ImVec2(150, 32));
                ImGui::EndDisabled();
            }
            else {
                if (ImGui::Button("Download Now", ImVec2(150, 32))) {
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
                ImGui::Button("Cancelling...");
                ImGui::EndDisabled();
            }
            else if (ImGui::Button("Cancel Active Download")) {
                m_downloader.cancel_download();
            }
        }
        else if (m_downloader.is_completed()) {
            scan_available_models();
            update_active_languages();
            std::string downloaded_path = m_downloader.get_downloaded_filepath();
            m_downloader.reset_completed_flag();

            if (!downloaded_path.empty()) {
                m_engine->load_model_async(downloaded_path, static_cast<uint32_t>(m_ctx_size));
                m_show_download_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (installed_count == 3) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "All 3 official models are installed!");
            ImGui::SameLine(ImGui::GetWindowWidth() - 210.0f);
            if (ImGui::Button("Start Application", ImVec2(190, 36))) {
                m_show_download_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        else {
            ImGui::Text("Installed: %d / 3 models", installed_count);
            ImGui::SameLine(ImGui::GetWindowWidth() - 230.0f);
            if (installed_count > 0 || !m_available_models.empty()) {
                if (ImGui::Button("Continue with Installed", ImVec2(210, 36))) {
                    m_show_download_modal = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            else {
                ImGui::TextDisabled("(Download at least 1 model to start)");
            }
        }

        ImGui::EndPopup();
    }
}

void AppUI::render_header_panel() {
    ImGui::TextColored(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), "Local AI Translation & Orthography Tool");
    ImGui::SameLine();
    ImGui::TextDisabled("(SDL3 + ImGui + OpenGL3 + libinfer)");

    render_donation_button();

    bool busy = m_engine->is_loading_model() || m_engine->is_generating();
    if (busy) ImGui::BeginDisabled();

    ImGui::PushItemWidth(360.0f);
    if (m_available_models.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "No .gguf models found!");
    }
    else {
        std::vector<const char*> model_ptrs;
        model_ptrs.reserve(m_available_models.size());
        for (const auto& m : m_available_models) model_ptrs.push_back(m.c_str());

        int prev_idx = m_selected_model_idx;
        if (ImGui::Combo("Active Model", &m_selected_model_idx, model_ptrs.data(), static_cast<int>(model_ptrs.size()))) {
            if (m_selected_model_idx != prev_idx && !m_available_models.empty()) {
                if (m_engine->load_model_async(m_available_models[m_selected_model_idx], static_cast<uint32_t>(m_ctx_size))) {
                    m_in_lang_idx = 0;
                    m_out_lang_idx = 1;
                    update_active_languages();
                }
                else {
                    m_selected_model_idx = prev_idx;
                }
            }
        }
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::PushItemWidth(90.0f);
    ImGui::InputInt("Context", &m_ctx_size, 0);
    ImGui::PopItemWidth();

    if (busy) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Download Menu")) {
        m_show_download_modal = true;
    }

    ImGui::SameLine();
    if (m_engine->is_loading_model()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[LOADING MODEL...]");
    }
    else if (m_engine->is_model_loaded()) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[Model Ready]");
    }
    else {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "[No Model]");
    }
}

void AppUI::render_controls_panel() {
    const char* sources[] = { "Window Text", "Clipboard", "Loaded File" };

    ImGui::PushItemWidth(140.0f);
    ImGui::Combo("Input Source", &m_input_source_idx, sources, IM_ARRAYSIZE(sources));
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 15.0f);

    ImGui::PushItemWidth(180.0f);
    ImGui::Combo("In Lang", &m_in_lang_idx, m_active_lang_ptrs.data(), static_cast<int>(m_active_lang_ptrs.size()));
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 15.0f);

    ImGui::PushItemWidth(180.0f);
    ImGui::Combo("Out Lang", &m_out_lang_idx, m_active_lang_ptrs.data(), static_cast<int>(m_active_lang_ptrs.size()));
    ImGui::PopItemWidth();

    if (m_input_source_idx == 2) {
        ImGui::SameLine(0, 15.0f);
        ImGui::PushItemWidth(180.0f);
        ImGui::InputText("Doc Path", m_doc_path_buf, sizeof(m_doc_path_buf));
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Load File")) {
            std::string err, content;
            if (DocParser::load_document(m_doc_path_buf, content, err)) {
                m_input_text = content;
                m_status_msg = "Document loaded into source box.";
                m_status_is_error = false;
            }
            else {
                m_status_msg = err;
                m_status_is_error = true;
            }
        }
    }

    // USER DIRECTIVE / PROMPT INSTRUCTIONS SECTION
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "AI Directive / Custom Instructions (Optional):");
    ImGui::SameLine();
    ImGui::TextDisabled("(Direct AI style, tone, terminology, or constraints)");

    float avail_width = ImGui::GetContentRegionAvail().x;
    float buttons_width = 300.0f;
    float input_width = std::max(220.0f, avail_width - buttons_width);

    ImGui::PushItemWidth(input_width);
    InputTextWithHintStdString(
        "##CustomDirective",
        "e.g. 'Use formal tone', 'Keep technical terms in English', 'Only correct punctuation'...",
        &m_custom_directive
    );
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 8.0f);
    if (ImGui::Button("Formal", ImVec2(65, 0))) {
        m_custom_directive = "Use a formal, polite, and professional tone throughout.";
    }
    ImGui::SameLine(0, 4.0f);
    if (ImGui::Button("Casual", ImVec2(65, 0))) {
        m_custom_directive = "Use a natural, conversational, and casual tone.";
    }
    ImGui::SameLine(0, 4.0f);
    if (ImGui::Button("Technical", ImVec2(80, 0))) {
        m_custom_directive = "Preserve all technical terminology, acronyms, and code identifiers verbatim.";
    }
    ImGui::SameLine(0, 4.0f);
    if (ImGui::Button("Clear", ImVec2(55, 0))) {
        m_custom_directive.clear();
    }
    ImGui::Spacing();

    // ACTION BUTTONS SECTION
    bool generating = m_engine->is_generating();
    bool loading_model = m_engine->is_loading_model();

    if (generating) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.28f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.12f, 0.12f, 1.0f));
        if (ImGui::Button(" STOP / CANCEL ", ImVec2(150, 36))) {
            m_engine->abort();
            m_status_msg = "Abort requested.";
            m_status_is_error = true;
        }
        ImGui::PopStyleColor(3);
    }
    else {
        if (!m_engine->is_model_loaded() || loading_model) ImGui::BeginDisabled();

        // TRANSLATE BUTTON
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.28f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.12f, 0.12f, 1.0f));

        if (ImGui::Button(" TRANSLATE ", ImVec2(150, 36))) {
            if (m_input_source_idx == 1) {
                const char* clipboard_text = ImGui::GetClipboardText();
                if (clipboard_text && std::strlen(clipboard_text) > 0) {
                    m_input_text = clipboard_text;
                }
                else {
                    m_status_msg = "Clipboard is empty!";
                    m_status_is_error = true;
                }
            }

            if (!m_input_text.empty()) {
                const auto& active_langs = get_active_model_supported_languages();
                std::string in_lang = active_langs[m_in_lang_idx];
                std::string out_lang = active_langs[m_out_lang_idx];

                if (m_engine->start_translation_task(in_lang, out_lang, m_input_text, m_custom_directive)) {
                    m_status_msg = "Translating " + in_lang + " to " + out_lang + "...";
                    m_status_is_error = false;
                }
                else {
                    m_status_msg = m_engine->get_last_error();
                    m_status_is_error = true;
                }
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 12.0f);

        // AUTO-CORRECT TYPOS BUTTON
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.60f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.70f, 0.98f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.48f, 0.70f, 1.0f));

        if (ImGui::Button(" AUTO-CORRECT TYPOS ", ImVec2(200, 36))) {
            if (m_input_source_idx == 1) {
                const char* clipboard_text = ImGui::GetClipboardText();
                if (clipboard_text && std::strlen(clipboard_text) > 0) {
                    m_input_text = clipboard_text;
                }
            }

            if (!m_input_text.empty()) {
                const auto& active_langs = get_active_model_supported_languages();
                std::string in_lang = active_langs[m_in_lang_idx];

                if (m_engine->start_auto_correct_task(in_lang, m_input_text, m_custom_directive)) {
                    m_status_msg = "Fixing typos & grammar in " + in_lang + "...";
                    m_status_is_error = false;
                }
                else {
                    m_status_msg = m_engine->get_last_error();
                    m_status_is_error = true;
                }
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 12.0f);

        if (ImGui::Button("Retry Mistake", ImVec2(130, 36))) {
            m_engine->clear_output_text();
            m_output_text.clear();
            const auto& active_langs = get_active_model_supported_languages();
            std::string in_lang = active_langs[m_in_lang_idx];
            std::string out_lang = active_langs[m_out_lang_idx];

            if (m_engine->start_translation_task(in_lang, out_lang, m_input_text, m_custom_directive)) {
                m_status_msg = "Retrying generation...";
                m_status_is_error = false;
            }
            else {
                m_status_msg = m_engine->get_last_error();
                m_status_is_error = true;
            }
        }

        if (!m_engine->is_model_loaded() || loading_model) ImGui::EndDisabled();
    }

    ImGui::SameLine(0, 12.0f);
    if (ImGui::Button("Clear All", ImVec2(90, 36))) {
        m_input_text.clear();
        m_engine->clear_output_text();
        m_output_text.clear();
        m_cached_input_words = 0;
        m_cached_input_chars = 0;
        m_cached_output_words = 0;
        m_cached_output_chars = 0;
        m_last_input_size = 0;
        m_status_msg = "Cleared editors.";
        m_status_is_error = false;
    }
}

void AppUI::render_text_editors_panel() {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float avail_height = ImGui::GetContentRegionAvail().y - 35.0f;
    float box_width = (avail_width - 15.0f) * 0.5f;

    ImGui::BeginChild("InputGroup", ImVec2(box_width, avail_height), true);
    ImGui::TextUnformatted("Source Input Text:");

    ImGui::SameLine();
    if (ImGui::SmallButton("Paste Clipboard")) {
        const char* clip = ImGui::GetClipboardText();
        if (clip) m_input_text = clip;
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);
    ImGui::TextDisabled("Words: %zu | Chars: %zu", m_cached_input_words, m_cached_input_chars);

    InputTextMultilineStdString("##SourceText", &m_input_text, ImVec2(-FLT_MIN, -FLT_MIN));
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("OutputGroup", ImVec2(box_width, avail_height), true);
    ImGui::TextUnformatted("Translated / Corrected Output Text:");
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &m_auto_scroll);

    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Output")) {
        ImGui::SetClipboardText(m_output_text.c_str());
        m_status_msg = "Copied output to system clipboard!";
        m_status_is_error = false;
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);
    ImGui::TextDisabled("Words: %zu | Chars: %zu", m_cached_output_words, m_cached_output_chars);

    ImGuiInputTextFlags out_flags = ImGuiInputTextFlags_ReadOnly;
    InputTextMultilineStdString("##OutputText", &m_output_text, ImVec2(-FLT_MIN, -FLT_MIN), out_flags);

    if (m_auto_scroll && m_engine->is_generating()) {
        ImGui::SetScrollHereY(1.0f);
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
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[PROCESSING...]");
        ImGui::SameLine();
    }

    if (m_status_is_error) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Error: %s", m_status_msg.c_str());
    }
    else {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Status: %s", m_status_msg.c_str());
    }
}