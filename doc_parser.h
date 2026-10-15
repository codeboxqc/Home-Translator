/* ========================================================================== */
/* doc_parser.h                                                               */
/* Document loading and parsing utilities for plain text and binary formats   */
/* ========================================================================== */
#ifndef DOC_PARSER_H
#define DOC_PARSER_H

#include <string>

class DocParser {
public:
    enum class FileType {
        TEXT,
        PDF,
        DOCX,
        UNKNOWN
    };

    /**
     * @brief Detect file type based on file extension.
     */
    static FileType detect_file_type(const std::string& filepath);

    /**
     * @brief Reads document content into UTF-8 text string safely.
     */
    static bool load_document(const std::string& filepath, std::string& out_text, std::string& out_error);

private:
    static bool read_text_file(const std::string& filepath, std::string& out_text, std::string& out_error);
    static bool read_pdf_file(const std::string& filepath, std::string& out_text, std::string& out_error);
    static bool read_docx_file(const std::string& filepath, std::string& out_text, std::string& out_error);
};

#endif // DOC_PARSER_H