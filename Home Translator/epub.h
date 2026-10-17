
#pragma once
/* ========================================================================== */
/* epub.h                                                                     */
/* LibZip-Powered Layout-Preserving EPUB Extractor & Repackager               */
/* ========================================================================== */
#ifndef EPUB_H
#define EPUB_H

#include <string>
#include <vector>
#include <cstddef>
#include <sstream>
#include <functional>
#include <unordered_map>

struct EpubSegment {
    size_t id;
    std::string original_text;
};

struct EpubChapter {
    std::string rel_path;
    std::string skeleton_xml; // Contains {{EPUB_SEG_#}} placeholders
};

struct EpubArchiveState {
    std::string source_epub_path;
    std::string temp_extract_dir;
    std::vector<EpubChapter> chapters;
    std::vector<EpubSegment> segments;
    bool is_active = false;
};

class EpubProcessor {
public:
    // Continuous File-by-File EPUB Processing API:
    static bool prepare_epub_extraction(
        const std::string& input_epub_filepath,
        std::string& out_temp_dir,
        std::vector<std::string>& out_rel_html_files,
        std::string& out_error
    );

    static bool extract_single_file_segments(
        const std::string& full_path,
        std::vector<EpubSegment>& out_segments,
        std::string& out_skeleton_xml,
        std::string& out_prompt_text
    );

    static bool write_single_file_translation(
        const std::string& full_path,
        const std::string& skeleton_xml,
        const std::vector<EpubSegment>& segments,
        const std::unordered_map<size_t, std::string>& translated_map
    );

    static bool finalize_epub_repack(
        const std::string& temp_dir,
        const std::string& output_epub_filepath,
        std::string& out_error
    );

    static std::unordered_map<size_t, std::string> parse_translated_markers(
        const std::string& text,
        size_t max_id
    );

    // Direct Tag-by-Tag / File-by-File callback API
    static bool translate_epub_tag_by_tag(
        const std::string& input_epub_filepath,
        const std::string& output_epub_filepath,
        std::function<std::string(const std::string& english_text)> ai_translate_fn,
        std::string& out_error
    );

    // Legacy Monolithic Unpack & Extract (Preserved for compatibility)
    static bool extract_for_translation(
        const std::string& epub_filepath,
        std::string& out_combined_prompt_text,
        EpubArchiveState& out_state,
        std::string& out_error
    );

    static bool rebuild_translated_epub(
        const std::string& output_epub_filepath,
        const std::string& translated_combined_text,
        const EpubArchiveState& state,
        std::string& out_error
    );

    static void cleanup_state(EpubArchiveState& state);

    // LibZip Archive Helpers
    static bool extract_zip_archive(
        const std::string& zip_path,
        const std::string& dest_dir,
        std::string& out_error
    );

    static bool create_epub_archive(
        const std::string& src_dir,
        const std::string& out_epub_path,
        std::string& out_error
    );

private:
    static bool process_xhtml_file(
        const std::string& full_path,
        const std::string& rel_path,
        EpubArchiveState& state,
        std::ostringstream& prompt_stream
    );
};

#endif // EPUB_H