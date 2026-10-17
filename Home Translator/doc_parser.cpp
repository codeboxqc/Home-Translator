/* ========================================================================== */
/* doc_parser.cpp                                                             */
/* Zero-Dependency Parser for TXT, SRT/VTT, DOCX, EPUB, JSON/PO & Exports     */
/* ========================================================================== */
#include "doc_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {
    void replace_all(std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    std::string decode_xml_entities(std::string s) {
        replace_all(s, "&amp;", "&");
        replace_all(s, "&lt;", "<");
        replace_all(s, "&gt;", ">");
        replace_all(s, "&quot;", "\"");
        replace_all(s, "&apos;", "'");
        replace_all(s, "&#39;", "'");
        replace_all(s, "&nbsp;", " ");
        return s;
    }

    int execute_tool_silent(const std::string& cmd_line) {
#if defined(_WIN32)
        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
        cmd_buf.push_back('\0');

        if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return -1;
        }
        // 10-second timeout avoids hanging the application
        DWORD wait_res = WaitForSingleObject(pi.hProcess, 10000);
        if (wait_res == WAIT_TIMEOUT) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return -1;
        }

        DWORD exit_code = 1;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return static_cast<int>(exit_code);
#else
        std::string redirected = cmd_line + " >/dev/null 2>&1";
        return std::system(redirected.c_str());
#endif
    }
} // anonymous namespace

DocParser::FileType DocParser::detect_file_type(const std::string& filepath) {
    size_t dot_pos = filepath.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return FileType::TEXT;
    }

    std::string ext = filepath.substr(dot_pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
        });

    if (ext == "pdf") return FileType::PDF;
    if (ext == "docx") return FileType::DOCX;
    if (ext == "epub") return FileType::EPUB;
    if (ext == "srt" || ext == "vtt") return FileType::SUBTITLE;
    if (ext == "json" || ext == "po" || ext == "ini" || ext == "yaml" || ext == "yml") {
        return FileType::LOCALIZATION;
    }
    if (ext == "txt" || ext == "md" || ext == "csv" || ext == "xml" ||
        ext == "html" || ext == "htm" || ext == "log" ||
        ext == "c" || ext == "cpp" || ext == "h" || ext == "hpp" || ext == "py") {
        return FileType::TEXT;
    }

    return FileType::UNKNOWN;
}



bool DocParser::save_html_document(
    const std::string& filepath,
    const std::string& title,
    const std::string& translated_text,
    std::string& out_error
) {
    std::ofstream out(filepath);
    if (!out.is_open()) {
        out_error = "Failed to open HTML file for writing: " + filepath;
        return false;
    }

    out << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset='utf-8'>\n"
        << "<title>" << title << "</title>\n"
        << "<style>\n"
        << "body { font-family: 'Segoe UI', system-ui, sans-serif; background: #0f1117; color: #e2e8f0; margin: 40px auto; max-width: 860px; line-height: 1.7; padding: 0 20px; }\n"
        << "h1 { color: #38bdf8; border-bottom: 2px solid #1e293b; padding-bottom: 12px; font-size: 24px; }\n"
        << "p { margin-bottom: 1.4em; font-size: 16px; background: #1e293b; padding: 14px 18px; border-radius: 8px; border-left: 4px solid #0284c7; }\n"
        << ".footer { margin-top: 50px; font-size: 12px; color: #64748b; text-align: center; border-top: 1px solid #1e293b; padding-top: 20px; }\n"
        << "</style>\n</head>\n<body>\n"
        << "<h1>" << title << "</h1>\n";

    std::istringstream iss(translated_text);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) {
            out << "<p>" << line << "</p>\n";
        }
    }

    out << "<div class='footer'>Generated with Local AI Translation & Publishing Suite Pro</div>\n"
        << "</body>\n</html>";
    return out.good();
}





bool DocParser::load_document(
    const std::string& filepath,
    std::string& out_text,
    std::string& out_error,
    std::vector<SubtitleCue>* out_srt_cues
) {
    out_text.clear();
    out_error.clear();
    if (out_srt_cues) out_srt_cues->clear();

    FileType ft = detect_file_type(filepath);

    switch (ft) {
    case FileType::SUBTITLE: {
        std::vector<SubtitleCue> local_cues;
        bool ok = read_subtitle_file(filepath, out_text, local_cues, out_error);
        if (ok && out_srt_cues) *out_srt_cues = std::move(local_cues);
        return ok;
    }
    case FileType::PDF:
        return read_pdf_file(filepath, out_text, out_error);
    case FileType::DOCX:
        return read_docx_file(filepath, out_text, out_error);
    case FileType::EPUB:
        return read_epub_file(filepath, out_text, out_error);
    case FileType::LOCALIZATION:
    case FileType::TEXT:
    case FileType::UNKNOWN:
    default:
        return read_text_file(filepath, out_text, out_error);
    }
}

bool DocParser::save_document(
    const std::string& filepath,
    const std::string& translated_text,
    const std::vector<SubtitleCue>& srt_cues,
    std::string& out_error
) {
    std::ofstream out(filepath, std::ios::out | std::ios::binary);
    if (!out.is_open()) {
        out_error = "Failed to open output file for writing: " + filepath;
        return false;
    }

    FileType ft = detect_file_type(filepath);
    if (!srt_cues.empty() && ft == FileType::SUBTITLE) {
        std::vector<std::string> translated_blocks;
        size_t start = 0;
        while (start < translated_text.size()) {
            size_t pos = translated_text.find("\n\n", start);
            if (pos == std::string::npos) {
                translated_blocks.push_back(translated_text.substr(start));
                break;
            }
            translated_blocks.push_back(translated_text.substr(start, pos - start));
            start = pos + 2;
            while (start < translated_text.size() && (translated_text[start] == '\n' || translated_text[start] == '\r')) {
                ++start;
            }
        }

        for (size_t i = 0; i < srt_cues.size(); ++i) {
            if (!srt_cues[i].index_str.empty()) {
                out << srt_cues[i].index_str << "\n";
            }
            out << srt_cues[i].timecode << "\n";
            if (i < translated_blocks.size()) {
                std::string clean_line = translated_blocks[i];
                if (!clean_line.empty() && clean_line[0] == '[') {
                    size_t close_br = clean_line.find("] ");
                    if (close_br != std::string::npos && close_br < 16) {
                        clean_line.erase(0, close_br + 2);
                    }
                }
                out << clean_line << "\n\n";
            }
            else {
                out << srt_cues[i].text << "\n\n";
            }
        }
        return true;
    }

    out.write(translated_text.data(), static_cast<std::streamsize>(translated_text.size()));
    return out.good();
}

bool DocParser::read_text_file(const std::string& filepath, std::string& out_text, std::string& out_error) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        out_error = "Failed to open file: " + filepath;
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streampos size = file.tellg();
    if (size < 0) {
        out_error = "Failed to determine file size for: " + filepath;
        return false;
    }

    if (size == 0) {
        out_text.clear();
        return true;
    }

    constexpr size_t MAX_TEXT_FILE_SIZE = 64 * 1024 * 1024; // 64 MB cap
    if (static_cast<size_t>(size) > MAX_TEXT_FILE_SIZE) {
        out_error = "File exceeds maximum supported plain text size (64 MB).";
        return false;
    }

    out_text.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(&out_text[0], size);

    if (!file && !file.eof()) {
        out_error = "Error occurred while reading file buffer: " + filepath;
        out_text.clear();
        return false;
    }

    if (out_text.size() >= 3 &&
        static_cast<unsigned char>(out_text[0]) == 0xEF &&
        static_cast<unsigned char>(out_text[1]) == 0xBB &&
        static_cast<unsigned char>(out_text[2]) == 0xBF) {
        out_text.erase(0, 3);
    }

    return true;
}

bool DocParser::read_subtitle_file(
    const std::string& filepath,
    std::string& out_text,
    std::vector<SubtitleCue>& out_cues,
    std::string& out_error
) {
    std::string raw;
    if (!read_text_file(filepath, raw, out_error)) {
        return false;
    }

    replace_all(raw, "\r\n", "\n");
    std::istringstream iss(raw);
    std::string line;
    std::ostringstream extracted;

    SubtitleCue current;
    bool in_cue = false;

    while (std::getline(iss, line)) {
        if (line == "WEBVTT") continue;

        if (line.find("-->") != std::string::npos) {
            current.timecode = line;
            in_cue = true;
            continue;
        }

        if (!in_cue) {
            if (!line.empty() && std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isdigit(c) || std::isspace(c); })) {
                current.index_str = line;
            }
        }
        else {
            if (line.empty()) {
                if (!current.text.empty()) {
                    out_cues.push_back(current);
                    extracted << "[" << out_cues.size() << "] " << current.text << "\n\n";
                }
                current = SubtitleCue{};
                in_cue = false;
            }
            else {
                if (!current.text.empty()) current.text += "\n";
                current.text += line;
            }
        }
    }

    if (in_cue && !current.text.empty()) {
        out_cues.push_back(current);
        extracted << "[" << out_cues.size() << "] " << current.text << "\n\n";
    }

    out_text = extracted.str();
    if (out_text.empty()) {
        out_text = raw;
    }
    return true;
}

std::string DocParser::strip_xml_tags(const std::string& xml_content, bool is_docx) {
    std::string result;
    result.reserve(xml_content.size() / 2);

    bool in_tag = false;
    bool skip_tag_body = false;
    std::string tag_name;

    for (size_t i = 0; i < xml_content.size(); ++i) {
        char c = xml_content[i];
        if (c == '<') {
            in_tag = true;
            tag_name.clear();
            continue;
        }
        if (c == '>') {
            in_tag = false;
            if (is_docx) {
                if (tag_name == "/w:p" || tag_name.rfind("w:br", 0) == 0) {
                    result += "\n\n";
                }
                else if (tag_name == "w:tab/" || tag_name == "w:tab") {
                    result += "\t";
                }
            }
            else {
                if (tag_name == "/p" || tag_name == "/div" || tag_name == "/h1" ||
                    tag_name == "/h2" || tag_name == "/h3" || tag_name.rfind("br", 0) == 0) {
                    result += "\n\n";
                }
                if (tag_name.rfind("style", 0) == 0 || tag_name.rfind("script", 0) == 0 || tag_name.rfind("head", 0) == 0) {
                    skip_tag_body = true;
                }
                else if (tag_name == "/style" || tag_name == "/script" || tag_name == "/head") {
                    skip_tag_body = false;
                }
            }
            continue;
        }

        if (in_tag) {
            if (!std::isspace(static_cast<unsigned char>(c)) || !tag_name.empty()) {
                if (tag_name.size() < 24) tag_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        else if (!skip_tag_body) {
            if (!is_docx && (c == '\n' || c == '\r')) continue;
            result += c;
        }
    }

    result = decode_xml_entities(result);
    replace_all(result, "\n\n\n", "\n\n");
    return result;
}

bool DocParser::read_docx_file(const std::string& filepath, std::string& out_text, std::string& out_error) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        out_error = "Failed to open DOCX file: " + filepath;
        return false;
    }

    char header[4] = { 0 };
    file.read(header, 4);
    file.close();

    if (!(header[0] == 'P' && header[1] == 'K' && header[2] == 0x03 && header[3] == 0x04)) {
        return read_text_file(filepath, out_text, out_error);
    }

    fs::path temp_dir = fs::temp_directory_path() / "home_translator_docx_tmp";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    std::string cmd = "tar -xf \"" + filepath + "\" -C \"" + temp_dir.string() + "\" word/document.xml";
    int rc = execute_tool_silent(cmd);
    fs::path doc_xml = temp_dir / "word" / "document.xml";

    if (rc != 0 || !fs::exists(doc_xml)) {
        fs::remove_all(temp_dir, ec);
        out_error = "Failed to extract word/document.xml from DOCX archive.";
        return false;
    }

    std::string xml_raw;
    bool ok = read_text_file(doc_xml.string(), xml_raw, out_error);
    fs::remove_all(temp_dir, ec);

    if (!ok) return false;
    out_text = strip_xml_tags(xml_raw, true);
    return true;
}

bool DocParser::read_epub_file(const std::string& filepath, std::string& out_text, std::string& out_error) {
    fs::path temp_dir = fs::temp_directory_path() / "home_translator_epub_tmp";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    std::string cmd = "tar -xf \"" + filepath + "\" -C \"" + temp_dir.string() + "\"";
    execute_tool_silent(cmd);

    std::vector<fs::path> chapters;
    for (const auto& entry : fs::recursive_directory_iterator(temp_dir, ec)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".xhtml" || ext == ".html" || ext == ".htm") {
                chapters.push_back(entry.path());
            }
        }
    }

    std::sort(chapters.begin(), chapters.end());
    std::ostringstream full_book;

    for (const auto& ch : chapters) {
        std::string raw_html, err;
        if (read_text_file(ch.string(), raw_html, err)) {
            std::string clean = strip_xml_tags(raw_html, false);
            if (!clean.empty()) {
                full_book << clean << "\n\n";
            }
        }
    }

    fs::remove_all(temp_dir, ec);
    out_text = full_book.str();
    if (out_text.empty()) {
        out_error = "Could not extract readable XHTML chapters from EPUB.";
        return false;
    }
    return true;
}

bool DocParser::read_pdf_file(const std::string& filepath, std::string& out_text, std::string& out_error) {
    out_text.clear();
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        out_error = "Failed to open PDF file: " + filepath;
        return false;
    }

    char header[5] = { 0 };
    file.read(header, 4);
    file.close();

    if (std::strncmp(header, "%PDF", 4) == 0) {
        fs::path temp_txt = fs::temp_directory_path() / "home_translator_pdf_tmp.txt";
        std::string cmd = "pdftotext -enc UTF-8 \"" + filepath + "\" \"" + temp_txt.string() + "\"";
        if (execute_tool_silent(cmd) == 0 && fs::exists(temp_txt)) {
            bool ok = read_text_file(temp_txt.string(), out_text, out_error);
            std::error_code ec;
            fs::remove(temp_txt, ec);
            if (ok && !out_text.empty()) return true;
        }
        out_error = "PDF binary detected. Install 'pdftotext' (Poppler) in PATH or convert PDF to .docx/.txt.";
        return false;
    }

    return read_text_file(filepath, out_text, out_error);
}