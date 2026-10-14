/* ========================================================================== */
/* doc_parser.cpp                                                             */
/* Implementation of document parser and format hooks                         */
/* ========================================================================== */
#include "doc_parser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

DocParser::FileType DocParser::detect_file_type(const std::string& filepath) {
    size_t dot_pos = filepath.find_last_of(".");
    if (dot_pos == std::string::npos) {
        return FileType::TEXT;
    }

    std::string ext = filepath.substr(dot_pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { 
        return static_cast<char>(std::tolower(c)); 
    });

    if (ext == "pdf") {
        return FileType::PDF;
    } else if (ext == "docx") {
        return FileType::DOCX;
    } else if (ext == "txt" || ext == "md" || ext == "csv" || ext == "json" ||
               ext == "xml" || ext == "html" || ext == "htm" || ext == "log") {
        return FileType::TEXT;
    }

    return FileType::UNKNOWN;
}

bool DocParser::load_document(const std::string& filepath, std::string& out_text, std::string& out_error) {
    out_text.clear();
    out_error.clear();

    FileType ft = detect_file_type(filepath);

    switch (ft) {
        case FileType::TEXT:
        case FileType::UNKNOWN:
            return read_text_file(filepath, out_text, out_error);
        case FileType::PDF:
            return read_pdf_file(filepath, out_text, out_error);
        case FileType::DOCX:
            return read_docx_file(filepath, out_text, out_error);
        default:
            return read_text_file(filepath, out_text, out_error);
    }
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

    out_text.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(&out_text[0], size);

    if (!file && !file.eof()) {
        out_error = "Error occurred while reading file buffer: " + filepath;
        return false;
    }

    // Strip UTF-8 Byte Order Mark (BOM) if present
    if (out_text.size() >= 3 &&
        static_cast<unsigned char>(out_text[0]) == 0xEF &&
        static_cast<unsigned char>(out_text[1]) == 0xBB &&
        static_cast<unsigned char>(out_text[2]) == 0xBF) {
        out_text.erase(0, 3);
    }

    return true;
}

bool DocParser::read_pdf_file(const std::string& filepath, std::string& out_text, std::string& out_error) {
    /* ========================================================================= */
    /* Poppler PDF Parser Integration Extension Hook                             */
    /* ========================================================================= */
    /* To enable full PDF text extraction using Poppler-CPP:                     */
    /* #include <poppler/cpp/poppler-document.h>                                  */
    /* #include <poppler/cpp/poppler-page.h>                                      */
    /*                                                                           */
    /* poppler::document* doc = poppler::document::load_from_file(filepath);     */
    /* if (!doc) { out_error = "Poppler failed to parse PDF"; return false; }   */
    /* int num_pages = doc->pages();                                             */
    /* std::ostringstream oss;                                                   */
    /* for (int i = 0; i < num_pages; ++i) {                                     */
    /*     poppler::page* p = doc->create_page(i);                               */
    /*     if (p) { oss << p->text().to_utf8().data() << "\n"; delete p; }        */
    /* }                                                                         */
    /* delete doc;                                                               */
    /* out_text = oss.str();                                                     */
    /* return true;                                                              */
    /* ========================================================================= */

    // Graceful fallback reading text stream directly or reporting backend requirement
    if (read_text_file(filepath, out_text, out_error)) {
        if (out_text.find("%PDF") == 0) {
            out_error = "PDF binary detected. Compile with Poppler (-lpoppler-cpp) to extract PDF text.";
            return false;
        }
        return true;
    }
    return false;
}

bool DocParser::read_docx_file(const std::string& filepath, std::string& out_text, std::string& out_error) {
    /* ========================================================================= */
    /* Minizip / libxml2 DOCX Parser Integration Extension Hook                  */
    /* ========================================================================= */
    /* DOCX files are PKZIP archives containing "word/document.xml".            */
    /* To enable full DOCX text extraction:                                      */
    /* 1. Open zip archive using minizip / libzip (e.g. unzOpen).                */
    /* 2. Extract buffer for "word/document.xml".                                */
    /* 3. Parse XML DOM tree and aggregate content inside <w:t> tags.            */
    /* 4. Store extracted string in out_text.                                    */
    /* ========================================================================= */

    out_error = "DOCX binary detected. Compile with minizip & libxml2 to extract word/document.xml.";
    return false;
}