/* ========================================================================== */
/* app_ui.cpp                                                                 */
/* Full UI implementation with Animated Progress Bar & Cancel Button         */
/* ========================================================================== */
#include "app_ui.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace {
    // UTF-8 aware ImGui Multiline InputText callback
    static int InputTextStdStringCallback(ImGuiInputTextCallbackData* data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            std::string* str = static_cast<std::string*>(data->UserData);
            str->resize(data->BufTextLen);
            data->Buf = &((*str)[0]);
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
        return ImGui::InputTextMultiline(
            label,
            const_cast<char*>(str->c_str()),
            str->capacity() + 1,
            size,
            flags,
            InputTextStdStringCallback,
            static_cast<void*>(str)
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
    scan_available_models();

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

    std::vector<std::string> search_paths = { "models", "." };
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
    auto presets = ModelDownloader::get_presets();
    int count = 0;
    for (const auto& p : presets) {
        if (is_file_present(p.filename)) count++;
    }
    return count;
}

std::vector<std::string> AppUI::get_active_model_supported_languages() const {
    if (m_available_models.empty() || m_selected_model_idx >= static_cast<int>(m_available_models.size())) {
        return { "French (Français)", "English" };
    }

    std::string active_path = m_available_models[m_selected_model_idx];

    // Llama-3.2-3B-Instruct (8 Languages)
    if (active_path.find("Llama-3.2") != std::string::npos) {
        return {
            "French (Français)", "English", "German (Allemand)", "Spanish (Espagnol)",
            "Italian (Italien)", "Portuguese (Portugais)", "Hindi", "Thai (Thaï)"
        };
    }

    // Qwen2.5-7B-Instruct (29 Languages)
    if (active_path.find("Qwen2.5") != std::string::npos) {
        return {
            "English", "French (Français)", "Spanish (Espagnol)", "German (Allemand)",
            "Italian (Italien)", "Portuguese (Portugais)", "Dutch (Néerlandais)", "Russian (Russe)",
            "Polish (Polonais)", "Czech (Tchèque)", "Arabic (Arabe)", "Turkish (Turc)",
            "Persian (Farsi/Persan)", "Hebrew (Hébreu)", "Chinese (Chinois)", "Japanese (Japonais)",
            "Korean (Coréen)", "Hindi", "Bengali", "Urdu (Ourdou)", "Vietnamese (Vietnamien)",
            "Thai (Thaï)", "Indonesian (Indonésien)", "Malay (Malais)", "Tagalog (Filipino)",
            "Khmer (Cambodgien)", "Lao (Laotien)", "Burmese (Birman)", "Cebuano (Visayan)"
        };
    }

    // Qwen3-8B (Comprehensive Multilingual List)
    return {
        "English", "French (Français)", "Spanish (Espagnol)", "German (Allemand)",
        "Italian (Italien)", "Portuguese (Portugais)", "Dutch (Néerlandais)", "Russian (Russe)",
        "Polish (Polonais)", "Czech (Tchèque)", "Arabic (Arabe)", "Turkish (Turc)",
        "Persian (Farsi/Persan)", "Hebrew (Hébreu)", "Chinese (Chinois)", "Japanese (Japonais)",
        "Korean (Coréen)", "Hindi", "Bengali", "Urdu (Ourdou)", "Vietnamese (Vietnamien)",
        "Thai (Thaï)", "Indonesian (Indonésien)", "Malay (Malais)", "Tagalog (Filipino)",
        "Khmer (Cambodgien)", "Lao (Laotien)", "Burmese (Birman)", "Cebuano (Visayan)",
        "Swedish (Suédois)", "Danish (Danois)", "Norwegian (Norvégien)", "Finnish (Finnois)",
        "Greek (Grec)", "Slovak (Slovaque)", "Hungarian (Hongrois)", "Romanian (Roumain)",
        "Bulgarian (Bulgare)", "Ukrainian (Ukrainien)", "Serbian (Serbe)", "Croatian (Croate)",
        "Lithuanian (Lituanien)", "Latvian (Letton)", "Estonian (Estonien)",
        "Tamil (Tamoul)", "Telugu (Télougou)", "Marathi", "Gujarati", "Punjabi (Pendjabi)",
        "Malayalam", "Kannada", "Sinhalese (Cinghalais)", "Nepali (Népalais)", "Pashto (Pachto)",
        "Kazakh", "Uzbek (Ouzbek)", "Azerbaijani (Azéri)", "Georgian (Géorgien)", "Armenian (Arménien)",
        "Javanese (Javanais)", "Swahili", "Amharic (Amharique)", "Yoruba", "Igbo",
        "Zulu (Zoulou)", "Hausa (Haoussa)", "Afrikaans", "Malagasy (Malgache)"
    };
}

size_t AppUI::count_words(const std::string& str) {
    std::istringstream iss(str);
    std::string word;
    size_t count = 0;
    while (iss >> word) count++;
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

    // Render Progress Bar & Cancel Button when generation is active
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

    ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);
    if (ImGui::Button(" Buy me a coffee ", ImVec2(150, 26))) {
        open_url("https://buymeacoffee.com/www.nutz.club");
    }

    ImGui::PopStyleColor(4);
}

void AppUI::render_progress_and_cancel_bar() {
    ImGui::Spacing();

    double t = ImGui::GetTime();
    float pulse = (static_cast<float>(std::sin(t * 4.0)) + 1.0f) * 0.5f;

    std::string out_text = m_engine->get_output_text();
    size_t words = count_words(out_text);
    size_t chars = out_text.size();

    char progress_str[128];
    snprintf(progress_str, sizeof(progress_str), "Generating Translation... (%zu words / %zu chars generated)", words, chars);

    float avail_w = ImGui::GetContentRegionAvail().x - 170.0f;

    // Animated Pulsing Progress Bar
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.6f + 0.3f * pulse, 0.9f, 1.0f));
    ImGui::ProgressBar(-1.0f, ImVec2(avail_w, 28.0f), progress_str);
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 10.0f);

    // Red Cancel / Abort Button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.28f, 0.28f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.12f, 0.12f, 1.0f));

    if (ImGui::Button(" CANCEL / STOP ", ImVec2(150, 28))) {
        m_engine->abort();
        m_status_msg = "Generation cancelled by user.";
        m_status_is_error = true;
    }

    ImGui::PopStyleColor(3);
    ImGui::Spacing();
}

void AppUI::render_download_modal() {
    ImGui::OpenPopup("AI Models Download Menu");
    ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("AI Models Download Menu", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto presets = ModelDownloader::get_presets();
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

            ImGui::SameLine(ImGui::GetWindowWidth() - 170.0f);

            if (installed) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "[ Installed ]");
            }
            else if (downloading) {
                ImGui::BeginDisabled();
                ImGui::Button("Downloading...", ImVec2(140, 28));
                ImGui::EndDisabled();
            }
            else {
                if (ImGui::Button("Download Now", ImVec2(140, 28))) {
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
            if (ImGui::Button("Cancel Active Download")) {
                m_downloader.cancel_download();
            }
        }
        else if (m_downloader.is_completed()) {
            scan_available_models();
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
            ImGui::SameLine(ImGui::GetWindowWidth() - 200.0f);
            if (ImGui::Button("Start Application", ImVec2(180, 35))) {
                m_show_download_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }
        else {
            ImGui::Text("Installed: %d / 3 models", installed_count);
            ImGui::SameLine(ImGui::GetWindowWidth() - 220.0f);
            if (installed_count > 0 || !m_available_models.empty()) {
                if (ImGui::Button("Continue with Installed", ImVec2(200, 35))) {
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

    ImGui::PushItemWidth(340.0f);
    if (m_available_models.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "No .gguf models found!");
    }
    else {
        std::vector<const char*> model_ptrs;
        for (const auto& m : m_available_models) model_ptrs.push_back(m.c_str());

        int prev_idx = m_selected_model_idx;
        if (ImGui::Combo("Active Model", &m_selected_model_idx, model_ptrs.data(), static_cast<int>(model_ptrs.size()))) {
            if (m_selected_model_idx != prev_idx && !m_available_models.empty()) {
                m_engine->load_model_async(m_available_models[m_selected_model_idx], static_cast<uint32_t>(m_ctx_size));
                m_in_lang_idx = 0;
                m_out_lang_idx = 1;
            }
        }
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::PushItemWidth(80.0f);
    ImGui::InputInt("Context", &m_ctx_size, 0);
    ImGui::PopItemWidth();

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

    std::vector<std::string> active_langs = get_active_model_supported_languages();
    std::vector<const char*> lang_ptrs;
    for (const auto& l : active_langs) lang_ptrs.push_back(l.c_str());

    if (m_in_lang_idx >= static_cast<int>(lang_ptrs.size())) m_in_lang_idx = 0;
    if (m_out_lang_idx >= static_cast<int>(lang_ptrs.size())) m_out_lang_idx = (lang_ptrs.size() > 1) ? 1 : 0;

    ImGui::PushItemWidth(130.0f);
    ImGui::Combo("Input Source", &m_input_source_idx, sources, IM_ARRAYSIZE(sources));
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 15.0f);

    ImGui::PushItemWidth(170.0f);
    ImGui::Combo("In Lang", &m_in_lang_idx, lang_ptrs.data(), static_cast<int>(lang_ptrs.size()));
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 15.0f);

    ImGui::PushItemWidth(170.0f);
    ImGui::Combo("Out Lang", &m_out_lang_idx, lang_ptrs.data(), static_cast<int>(lang_ptrs.size()));
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 15.0f);

    if (m_input_source_idx == 2) {
        ImGui::PushItemWidth(160.0f);
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
        ImGui::SameLine(0, 15.0f);
    }

    bool generating = m_engine->is_generating();
    bool loading_model = m_engine->is_loading_model();

    if (generating) {
        // Red Stop / Abort Button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
        if (ImGui::Button(" STOP ", ImVec2(120, 32))) {
            m_engine->abort();
            m_status_msg = "Abort requested.";
            m_status_is_error = true;
        }
        ImGui::PopStyleColor();
    }
    else {
        if (!m_engine->is_model_loaded() || loading_model) ImGui::BeginDisabled();

        // RED "TRANSLATE" BUTTON
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.28f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.12f, 0.12f, 1.0f));

        if (ImGui::Button(" TRANSLATE ", ImVec2(140, 32))) {
            if (m_input_source_idx == 1) { // System Clipboard
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
                std::string in_lang = active_langs[m_in_lang_idx];
                std::string out_lang = active_langs[m_out_lang_idx];

                if (m_engine->start_translation_task(in_lang, out_lang, m_input_text)) {
                    if (in_lang == out_lang) {
                        m_status_msg = "Correcting " + in_lang + " grammar & orthography...";
                    }
                    else {
                        m_status_msg = "Translating " + in_lang + " to " + out_lang + "...";
                    }
                    m_status_is_error = false;
                }
                else {
                    m_status_msg = m_engine->get_last_error();
                    m_status_is_error = true;
                }
            }
        }

        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 10.0f);

        if (ImGui::Button("Retry Mistake", ImVec2(110, 32))) {
            m_engine->clear_output_text();
            std::string in_lang = active_langs[m_in_lang_idx];
            std::string out_lang = active_langs[m_out_lang_idx];

            if (m_engine->start_translation_task(in_lang, out_lang, m_input_text)) {
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

    ImGui::SameLine(0, 10.0f);
    if (ImGui::Button("Clear All", ImVec2(80, 32))) {
        m_input_text.clear();
        m_engine->clear_output_text();
    }
}

void AppUI::render_text_editors_panel() {
    float avail_width = ImGui::GetContentRegionAvail().x;
    float avail_height = ImGui::GetContentRegionAvail().y - 35.0f;
    float box_width = (avail_width - 15.0f) * 0.5f;

    // Left Column: Source Input Box
    ImGui::BeginChild("InputGroup", ImVec2(box_width, avail_height), true);
    ImGui::TextUnformatted("Source Input Text:");

    ImGui::SameLine();
    if (ImGui::SmallButton("Paste Clipboard")) {
        const char* clip = ImGui::GetClipboardText();
        if (clip) m_input_text = clip;
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
    ImGui::TextDisabled("Words: %zu | Chars: %zu", count_words(m_input_text), m_input_text.size());

    InputTextMultilineStdString("##SourceText", &m_input_text, ImVec2(-FLT_MIN, -FLT_MIN));
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Column: Output Read-Only Box
    ImGui::BeginChild("OutputGroup", ImVec2(box_width, avail_height), true);
    std::string output_text = m_engine->get_output_text();

    ImGui::TextUnformatted("Translated Output Text:");
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &m_auto_scroll);

    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Output")) {
        ImGui::SetClipboardText(output_text.c_str());
        m_status_msg = "Copied output to system clipboard!";
        m_status_is_error = false;
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
    ImGui::TextDisabled("Words: %zu | Chars: %zu", count_words(output_text), output_text.size());

    ImGuiInputTextFlags out_flags = ImGuiInputTextFlags_ReadOnly;
    InputTextMultilineStdString("##OutputText", &output_text, ImVec2(-FLT_MIN, -FLT_MIN), out_flags);

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