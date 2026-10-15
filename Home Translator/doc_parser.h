/* ========================================================================== */
/* doc_parser.h                                                               */
/* Pro Document Parser: Plain Text, SRT/VTT Subtitles, DOCX, EPUB & Export    */
/* ========================================================================== */
#ifndef DOC_PARSER_H
#define DOC_PARSER_H

#include <string>
#include <vector>

struct SubtitleCue {
    std::string index_str;
    std::string timecode;
    std::string text;
};

class DocParser {
public:
    enum class FileType {
        TEXT,
        SUBTITLE,
        LOCALIZATION,
        PDF,
        DOCX,
        EPUB,
        UNKNOWN
    };

    static FileType detect_file_type(const std::string& filepath);

    static bool load_document(
        const std::string& filepath,
        std::string& out_text,
        std::string& out_error,
        std::vector<SubtitleCue>* out_srt_cues = nullptr
    );

    static bool save_document(
        const std::string& filepath,
        const std::string& translated_text,
        const std::vector<SubtitleCue>& srt_cues,
        std::string& out_error
    );

private:
    static bool read_text_file(const std::string& filepath, std::string& out_text, std::string& out_error);
    static bool read_subtitle_file(
        const std::string& filepath,
        std::string& out_text,
        std::vector<SubtitleCue>& out_cues,
        std::string& out_error
    );
    static bool read_pdf_file(const std::string& filepath, std::string& out_text, std::string& out_error);
    static bool read_docx_file(const std::string& filepath, std::string& out_text, std::string& out_error);
    static bool read_epub_file(const std::string& filepath, std::string& out_text, std::string& out_error);

    static std::string strip_xml_tags(const std::string& xml_content, bool is_docx);
};

#endif // DOC_PARSER_H