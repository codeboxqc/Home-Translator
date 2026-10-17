
/* ========================================================================== */
/* epub.cpp                                                                   */
/* LibZip-Powered Layout-Preserving EPUB Extractor & Repackager               */
/* ========================================================================== */
#include "epub.h"

#include <zip.h> // Native LibZip Header

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

    bool read_raw_file(const fs::path& p, std::string& out) {
        std::ifstream f(p, std::ios::in | std::ios::binary);
        if (!f.is_open()) return false;
        f.seekg(0, std::ios::end);
        std::streampos sz = f.tellg();
        if (sz < 0) return false;
        out.resize(static_cast<size_t>(sz));
        f.seekg(0, std::ios::beg);
        if (sz > 0) f.read(&out[0], sz);
        return true;
    }

    bool write_raw_file(const fs::path& p, const std::string& data) {
        std::ofstream f(p, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        return f.good();
    }

    std::string to_lower_ascii(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
        return out;
    }

    std::string clean_whitespace(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        bool prev_space = false;
        for (unsigned char c : s) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (!prev_space) {
                    out.push_back(' ');
                    prev_space = true;
                }
            }
            else {
                out.push_back(static_cast<char>(c));
                prev_space = false;
            }
        }
        if (!out.empty() && out.front() == ' ') out.erase(out.begin());
        if (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    bool has_translatable_content(const std::string& s) {
        std::string trimmed = clean_whitespace(s);
        if (trimmed.empty()) return false;

        std::string low = to_lower_ascii(trimmed);
        if (trimmed.find(' ') == std::string::npos) {
            if (low.rfind("http://", 0) == 0 || low.rfind("https://", 0) == 0 || low.rfind("www.", 0) == 0) {
                return false;
            }
            if (low.rfind("reading-", 0) == 0 || low.rfind("file-", 0) == 0) {
                return false;
            }
        }

        for (size_t i = 0; i < trimmed.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(trimmed[i]);
            if (c == '&') {
                size_t semi = trimmed.find(';', i + 1);
                if (semi != std::string::npos && (semi - i) <= 10) {
                    i = semi;
                    continue;
                }
            }
            if (std::isalpha(c) || c >= 0x80) {
                return true;
            }
        }
        return false;
    }

    std::string escape_xml_text(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 16);
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '&') {
                size_t semi = s.find(';', i + 1);
                if (semi != std::string::npos && (semi - i) >= 2 && (semi - i) <= 10) {
                    bool valid_entity = true;
                    for (size_t k = i + 1; k < semi; ++k) {
                        unsigned char ec = static_cast<unsigned char>(s[k]);
                        if (!std::isalnum(ec) && ec != '#') {
                            valid_entity = false;
                            break;
                        }
                    }
                    if (valid_entity) {
                        out.push_back('&');
                        continue;
                    }
                }
                out += "&amp;";
            }
            else if (c == '<') {
                out += "&lt;";
            }
            else if (c == '>') {
                out += "&gt;";
            }
            else {
                out.push_back(c);
            }
        }
        return out;
    }

    std::string extract_local_tag_name(const std::string& tag_full, bool& out_is_closing, bool& out_is_self_closing) {
        out_is_closing = false;
        out_is_self_closing = false;
        if (tag_full.size() < 2 || tag_full.front() != '<') return "";

        if (tag_full.size() >= 2 && tag_full[tag_full.size() - 2] == '/') {
            out_is_self_closing = true;
        }

        size_t k = 1;
        while (k < tag_full.size() && std::isspace(static_cast<unsigned char>(tag_full[k]))) k++;
        if (k < tag_full.size() && tag_full[k] == '/') {
            out_is_closing = true;
            k++;
        }
        else if (k < tag_full.size() && (tag_full[k] == '!' || tag_full[k] == '?')) {
            return "";
        }

        std::string tag_lower;
        while (k < tag_full.size() && !std::isspace(static_cast<unsigned char>(tag_full[k])) &&
            tag_full[k] != '>' && tag_full[k] != '/') {
            tag_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(tag_full[k]))));
            k++;
        }

        size_t colon = tag_lower.find(':');
        if (colon != std::string::npos) {
            tag_lower = tag_lower.substr(colon + 1);
        }
        return tag_lower;
    }

} // anonymous namespace

std::unordered_map<size_t, std::string> EpubProcessor::parse_translated_markers(const std::string& text, size_t max_id) {
    std::unordered_map<size_t, std::string> map;
    const size_t n = text.size();

    auto find_marker = [&](size_t from, size_t& m_start, size_t& m_end, size_t& id_out) -> bool {
        size_t pos = from;
        while (pos < n) {
            size_t br = text.find('[', pos);
            if (br == std::string::npos) return false;
            size_t k = br + 1;
            while (k < n && (text[k] == ' ' || text[k] == '\t')) k++;
            if (k < n && (text[k] == 'E' || text[k] == 'e')) {
                k++;
                while (k < n && (text[k] == ' ' || text[k] == '\t')) k++;
                size_t d_start = k;
                while (k < n && std::isdigit(static_cast<unsigned char>(text[k]))) k++;
                size_t d_end = k;
                while (k < n && (text[k] == ' ' || text[k] == '\t')) k++;
                if (d_end > d_start && k < n && text[k] == ']') {
                    size_t pid = static_cast<size_t>(std::strtoull(text.substr(d_start, d_end - d_start).c_str(), nullptr, 10));
                    if (pid >= 1 && (max_id == 0 || pid <= max_id)) {
                        m_start = br;
                        m_end = k + 1;
                        id_out = pid;
                        return true;
                    }
                }
            }
            pos = br + 1;
        }
        return false;
        };

    size_t cur_s = 0, cur_e = 0, cur_id = 0;
    if (!find_marker(0, cur_s, cur_e, cur_id)) return map;

    while (cur_e <= n) {
        size_t c_start = cur_e;
        while (c_start < n && (text[c_start] == '*' || text[c_start] == '_' || text[c_start] == ':' || text[c_start] == ' ' || text[c_start] == '\t')) {
            c_start++;
        }

        size_t next_s = 0, next_e = 0, next_id = 0;
        bool has_next = find_marker(c_start, next_s, next_e, next_id);
        size_t c_end = has_next ? next_s : n;

        while (c_end > c_start && (text[c_end - 1] == '*' || text[c_end - 1] == '_')) {
            c_end--;
        }

        std::string val = clean_whitespace(text.substr(c_start, c_end - c_start));
        if (!val.empty()) {
            map[cur_id] = val;
        }

        if (!has_next) break;
        cur_s = next_s;
        cur_e = next_e;
        cur_id = next_id;
    }

    return map;
}

bool EpubProcessor::prepare_epub_extraction(
    const std::string& input_epub_filepath,
    std::string& out_temp_dir,
    std::vector<std::string>& out_rel_html_files,
    std::string& out_error
) {
    if (!fs::exists(input_epub_filepath)) {
        out_error = "EPUB file does not exist: " + input_epub_filepath;
        return false;
    }

    static std::atomic<uint64_t> temp_counter{ 0 };
    fs::path temp_dir = fs::temp_directory_path() / ("home_translator_epub_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(++temp_counter));

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir, ec);

    if (!extract_zip_archive(input_epub_filepath, temp_dir.string(), out_error)) {
        fs::remove_all(temp_dir, ec);
        return false;
    }

    out_temp_dir = temp_dir.string();

    std::vector<fs::path> html_paths;
    for (const auto& entry : fs::recursive_directory_iterator(temp_dir, ec)) {
        if (entry.is_regular_file()) {
            std::string ext = to_lower_ascii(entry.path().extension().string());
            if (ext == ".xhtml" || ext == ".html" || ext == ".htm") {
                html_paths.push_back(entry.path());
            }
        }
    }

    std::sort(html_paths.begin(), html_paths.end());

    out_rel_html_files.clear();
    for (const auto& p : html_paths) {
        out_rel_html_files.push_back(fs::relative(p, temp_dir, ec).generic_string());
    }

    if (out_rel_html_files.empty()) {
        fs::remove_all(temp_dir, ec);
        out_error = "No HTML/XHTML files found in EPUB archive.";
        return false;
    }

    return true;
}

bool EpubProcessor::extract_single_file_segments(
    const std::string& full_path,
    std::vector<EpubSegment>& out_segments,
    std::string& out_skeleton_xml,
    std::string& out_prompt_text
) {
    out_segments.clear();
    out_skeleton_xml.clear();
    out_prompt_text.clear();

    std::string xml;
    if (!read_raw_file(full_path, xml) || xml.empty()) return false;

    std::ostringstream prompt_stream;
    std::string skeleton;
    skeleton.reserve(xml.size() + 1024);

    size_t i = 0;
    const size_t n = xml.size();
    bool in_ignored_block = false;
    std::string ignored_tag_name;

    while (i < n) {
        if (in_ignored_block) {
            size_t next_lt = xml.find('<', i);
            if (next_lt == std::string::npos) {
                skeleton.append(xml, i, n - i);
                break;
            }
            size_t tag_end = xml.find('>', next_lt);
            if (tag_end == std::string::npos) {
                skeleton.append(xml, i, n - i);
                break;
            }

            std::string candidate_tag = xml.substr(next_lt, tag_end - next_lt + 1);
            bool is_closing = false, is_self = false;
            std::string local_name = extract_local_tag_name(candidate_tag, is_closing, is_self);

            skeleton.append(xml, i, tag_end - i + 1);
            i = tag_end + 1;

            if (is_closing && local_name == ignored_tag_name) {
                in_ignored_block = false;
            }
            continue;
        }

        if (xml[i] == '<') {
            if (xml.compare(i, 4, "<!--") == 0) {
                size_t end_c = xml.find("-->", i + 4);
                size_t len = (end_c == std::string::npos) ? (n - i) : (end_c + 3 - i);
                skeleton.append(xml, i, len);
                i += len;
                continue;
            }
            if (xml.compare(i, 9, "<![CDATA[") == 0) {
                size_t end_cd = xml.find("]]>", i + 9);
                size_t len = (end_cd == std::string::npos) ? (n - i) : (end_cd + 3 - i);
                skeleton.append(xml, i, len);
                i += len;
                continue;
            }

            size_t tag_end = xml.find('>', i);
            if (tag_end == std::string::npos) {
                skeleton.append(xml, i, n - i);
                break;
            }

            std::string tag_full = xml.substr(i, tag_end - i + 1);
            skeleton += tag_full;

            bool is_closing = false, is_self_closing = false;
            std::string tag_lower = extract_local_tag_name(tag_full, is_closing, is_self_closing);

            if (!is_closing && !is_self_closing) {
                if (tag_lower == "head" || tag_lower == "style" || tag_lower == "script" ||
                    tag_lower == "svg" || tag_lower == "math") {
                    in_ignored_block = true;
                    ignored_tag_name = tag_lower;
                }
            }

            i = tag_end + 1;
        }
        else {
            size_t next_lt = xml.find('<', i);
            if (next_lt == std::string::npos) next_lt = n;

            std::string text_node = xml.substr(i, next_lt - i);

            if (has_translatable_content(text_node)) {
                size_t first_non_ws = text_node.find_first_not_of(" \t\r\n");
                size_t last_non_ws = text_node.find_last_not_of(" \t\r\n");

                std::string leading_ws = text_node.substr(0, first_non_ws);
                std::string core_text = text_node.substr(first_non_ws, last_non_ws - first_non_ws + 1);
                std::string trailing_ws = text_node.substr(last_non_ws + 1);

                std::string clean_core = clean_whitespace(core_text);

                size_t seg_id = out_segments.size() + 1;
                out_segments.push_back({ seg_id, clean_core });

                skeleton += leading_ws + "{{EPUB_SEG_" + std::to_string(seg_id) + "}}" + trailing_ws;
                prompt_stream << "[E" << seg_id << "] " << clean_core << "\n\n";
            }
            else {
                skeleton += text_node;
            }

            i = next_lt;
        }
    }

    out_skeleton_xml = std::move(skeleton);
    out_prompt_text = prompt_stream.str();
    return !out_segments.empty();
}

bool EpubProcessor::write_single_file_translation(
    const std::string& full_path,
    const std::string& skeleton_xml,
    const std::vector<EpubSegment>& segments,
    const std::unordered_map<size_t, std::string>& translated_map
) {
    std::string rebuilt_xml = skeleton_xml;

    size_t pos = 0;
    while ((pos = rebuilt_xml.find("{{EPUB_SEG_", pos)) != std::string::npos) {
        size_t end_br = rebuilt_xml.find("}}", pos + 11);
        if (end_br == std::string::npos) break;

        size_t id = static_cast<size_t>(std::strtoull(
            rebuilt_xml.substr(pos + 11, end_br - (pos + 11)).c_str(), nullptr, 10
        ));

        std::string replacement;
        auto it = translated_map.find(id);
        if (it != translated_map.end() && !it->second.empty()) {
            replacement = escape_xml_text(it->second);
        }
        else if (id >= 1 && id <= segments.size()) {
            replacement = escape_xml_text(segments[id - 1].original_text);
        }

        rebuilt_xml.replace(pos, (end_br + 2) - pos, replacement);
        pos += replacement.size();
    }

    return write_raw_file(full_path, rebuilt_xml);
}

bool EpubProcessor::finalize_epub_repack(
    const std::string& temp_dir,
    const std::string& output_epub_filepath,
    std::string& out_error
) {
    std::error_code ec;
    fs::path abs_out = fs::absolute(output_epub_filepath, ec);
    if (fs::exists(abs_out, ec)) {
        fs::remove(abs_out, ec);
    }
    fs::create_directories(abs_out.parent_path(), ec);

    bool ok = create_epub_archive(temp_dir, abs_out.string(), out_error);
    fs::remove_all(temp_dir, ec);
    return ok;
}

bool EpubProcessor::translate_epub_tag_by_tag(
    const std::string& input_epub_filepath,
    const std::string& output_epub_filepath,
    std::function<std::string(const std::string& english_text)> ai_translate_fn,
    std::string& out_error
) {
    std::string temp_dir;
    std::vector<std::string> rel_files;
    if (!prepare_epub_extraction(input_epub_filepath, temp_dir, rel_files, out_error)) {
        return false;
    }

    for (const auto& rel_file : rel_files) {
        fs::path full_path = fs::path(temp_dir) / fs::u8path(rel_file);
        std::vector<EpubSegment> segments;
        std::string skeleton, prompt_text;
        if (!extract_single_file_segments(full_path.string(), segments, skeleton, prompt_text)) {
            continue;
        }

        std::unordered_map<size_t, std::string> translated_map;
        for (const auto& seg : segments) {
            std::string trans = ai_translate_fn(seg.original_text);
            if (!trans.empty()) {
                translated_map[seg.id] = trans;
            }
        }

        write_single_file_translation(full_path.string(), skeleton, segments, translated_map);
    }

    return finalize_epub_repack(temp_dir, output_epub_filepath, out_error);
}

/* ========================================================================== */
/* LibZip Helper Implementations                                              */
/* ========================================================================== */

bool EpubProcessor::extract_zip_archive(
    const std::string& zip_path,
    const std::string& dest_dir,
    std::string& out_error
) {
    int err_code = 0;
    zip_t* archive = zip_open(zip_path.c_str(), ZIP_RDONLY, &err_code);
    if (!archive) {
        zip_error_t zerror;
        zip_error_init_with_code(&zerror, err_code);
        out_error = std::string("LibZip failed to open archive: ") + zip_error_strerror(&zerror);
        zip_error_fini(&zerror);
        return false;
    }

    zip_int64_t num_entries = zip_get_num_entries(archive, 0);
    std::vector<char> buffer(65536);

    for (zip_int64_t i = 0; i < num_entries; ++i) {
        struct zip_stat st;
        if (zip_stat_index(archive, i, 0, &st) != 0) continue;

        fs::path out_file_path = fs::path(dest_dir) / fs::u8path(st.name);

        size_t name_len = std::strlen(st.name);
        if (name_len > 0 && (st.name[name_len - 1] == '/' || st.name[name_len - 1] == '\\')) {
            std::error_code ec;
            fs::create_directories(out_file_path, ec);
            continue;
        }

        std::error_code ec;
        fs::create_directories(out_file_path.parent_path(), ec);

        zip_file_t* zf = zip_fopen_index(archive, i, 0);
        if (!zf) continue;

        std::ofstream out(out_file_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            zip_fclose(zf);
            continue;
        }

        zip_int64_t bytes_read = 0;
        while ((bytes_read = zip_fread(zf, buffer.data(), buffer.size())) > 0) {
            out.write(buffer.data(), bytes_read);
        }

        out.close();
        zip_fclose(zf);
    }

    zip_close(archive);
    return true;
}

bool EpubProcessor::create_epub_archive(
    const std::string& src_dir,
    const std::string& out_epub_path,
    std::string& out_error
) {
    int err_code = 0;
    zip_t* archive = zip_open(out_epub_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err_code);
    if (!archive) {
        out_error = "LibZip failed to create target output EPUB file.";
        return false;
    }

    std::vector<std::unique_ptr<std::string>> file_buffers;
    file_buffers.reserve(512);

    auto add_file_to_zip = [&](const fs::path& full_path, const std::string& zip_entry_name, zip_int32_t comp_method) {
        auto content = std::make_unique<std::string>();
        if (!read_raw_file(full_path, *content)) return;

        zip_source_t* src = zip_source_buffer(archive, content->data(), content->size(), 0);
        if (src) {
            zip_int64_t idx = zip_file_add(archive, zip_entry_name.c_str(), src, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
            if (idx >= 0) {
                zip_set_file_compression(archive, idx, comp_method, 0);
                file_buffers.push_back(std::move(content));
            }
            else {
                zip_source_free(src);
            }
        }
        };

    fs::path mimetype_path = fs::path(src_dir) / "mimetype";
    if (fs::exists(mimetype_path)) {
        add_file_to_zip(mimetype_path, "mimetype", ZIP_CM_STORE);
    }

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(src_dir, ec)) {
        if (!entry.is_regular_file()) continue;

        std::string rel_path = fs::relative(entry.path(), src_dir, ec).generic_string();
        if (rel_path == "mimetype") continue;

        add_file_to_zip(entry.path(), rel_path, ZIP_CM_DEFAULT);
    }

    if (zip_close(archive) < 0) {
        out_error = "LibZip failed to write and finalize the EPUB archive.";
        return false;
    }

    return true;
}

void EpubProcessor::cleanup_state(EpubArchiveState& state) {
    if (!state.temp_extract_dir.empty()) {
        std::error_code ec;
        fs::remove_all(state.temp_extract_dir, ec);
    }
    state.chapters.clear();
    state.segments.clear();
    state.source_epub_path.clear();
    state.temp_extract_dir.clear();
    state.is_active = false;
}

bool EpubProcessor::process_xhtml_file(
    const std::string& full_path,
    const std::string& rel_path,
    EpubArchiveState& state,
    std::ostringstream& prompt_stream
) {
    std::vector<EpubSegment> segs;
    std::string skel, prompt;
    if (!extract_single_file_segments(full_path, segs, skel, prompt)) {
        return false;
    }

    for (const auto& s : segs) {
        state.segments.push_back(s);
        prompt_stream << "[E" << s.id << "] " << s.original_text << "\n\n";
    }

    state.chapters.push_back({ rel_path, std::move(skel) });
    return true;
}

bool EpubProcessor::extract_for_translation(
    const std::string& epub_filepath,
    std::string& out_combined_prompt_text,
    EpubArchiveState& out_state,
    std::string& out_error
) {
    cleanup_state(out_state);

    std::string temp_dir;
    std::vector<std::string> rel_html_files;
    if (!prepare_epub_extraction(epub_filepath, temp_dir, rel_html_files, out_error)) {
        return false;
    }

    out_state.source_epub_path = epub_filepath;
    out_state.temp_extract_dir = temp_dir;

    std::ostringstream prompt_stream;
    for (const auto& rel_file : rel_html_files) {
        fs::path full_path = fs::path(temp_dir) / fs::u8path(rel_file);
        process_xhtml_file(full_path.string(), rel_file, out_state, prompt_stream);
    }

    out_combined_prompt_text = prompt_stream.str();
    if (out_state.segments.empty()) {
        cleanup_state(out_state);
        out_error = "No translatable text elements found in EPUB document.";
        return false;
    }

    out_state.is_active = true;
    return true;
}

bool EpubProcessor::rebuild_translated_epub(
    const std::string& output_epub_filepath,
    const std::string& translated_combined_text,
    const EpubArchiveState& state,
    std::string& out_error
) {
    if (!state.is_active || state.temp_extract_dir.empty() || !fs::exists(state.temp_extract_dir)) {
        out_error = "No active EPUB working context found.";
        return false;
    }

    auto translated_map = parse_translated_markers(translated_combined_text, state.segments.size());

    for (const auto& ch : state.chapters) {
        fs::path full_chapter_path = fs::path(state.temp_extract_dir) / fs::u8path(ch.rel_path);
        if (!write_single_file_translation(full_chapter_path.string(), ch.skeleton_xml, state.segments, translated_map)) {
            out_error = "Failed to update XHTML file: " + ch.rel_path;
            return false;
        }
    }

    return finalize_epub_repack(state.temp_extract_dir, output_epub_filepath, out_error);
}