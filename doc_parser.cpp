/* ========================================================================== */
/* doc_parser.cpp                                                             */
/* Optimized document parser with zero-copy magic-byte validation            */
/* ========================================================================== */
#include "doc_parser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

DocParser::FileType DocParser::detect_file_type(const std::string& filepath) {
    size_t dot_pos = filepath.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return FileType::TEXT;
    }

    std::string ext = filepath.substr(dot_pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
        });

    if (ext == "pdf") {
        return FileType::PDF;
    }
    if (ext == "docx") {
        return FileType::DOCX;
    }
    if (ext == "txt" || ext == "md" || ext == "csv" || ext == "json" ||
        ext == "xml" || ext == "html" || ext == "htm" || ext == "log" ||
        ext == "c" || ext == "cpp" || ext == "h" || ext == "hpp" || ext == "py") {
        return FileType::TEXT;
    }

    return FileType::UNKNOWN;
}

bool DocParser::load_document(const std::string& filepath, std::string& out_text, std::string& out_error) {
    out_text.clear();
    out_error.clear();

    FileType ft = detect_file_type(filepath);

    switch (ft) {
    case FileType::PDF:
        return read_pdf_file(filepath, out_text, out_error);
    case FileType::DOCX:
        return read_docx_file(filepath, out_text, out_error);
    case FileType::TEXT:
    case FileType::UNKNOWN:
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

    if (size == 0) {
        out_text.clear();
        return true;
    }

    // Guard against reading multi-gigabyte files directly into memory
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

    // Strip UTF-8 Byte Order Mark (BOM) if present
    if (out_text.size() >= 3 &&
        static_cast<unsigned char>(out_text[0]) == 0xEF &&
        static_cast<unsigned char>(out_text[1]) == 0xBB &&
        static_cast<unsigned char>(out_text[2]) == 0xBF) {
        out_text.erase(0, 3);
    }

    return true;
}

// Memory-safe inspection: validates header before allocating memory
bool DocParser::read_pdf_file(const std::string& filepath, std::string& out_text, std::string& out_error) {
    out_text.clear();
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        out_error = "Failed to open PDF file: " + filepath;
        return false;
    }

    char header[5] = { 0 };
    file.read(header, 4);
    if (std::strncmp(header, "%PDF", 4) == 0) {
        out_error = "PDF binary detected. Please convert PDF to UTF-8 text or compile with Poppler (-lpoppler-cpp).";
        return false;
    }

    file.close();
    return read_text_file(filepath, out_text, out_error);
}

// Memory-safe inspection: validates ZIP magic bytes without full file read
bool DocParser::read_docx_file(const std::string& filepath, std::string& out_text, std::string& out_error) {
    out_text.clear();
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        out_error = "Failed to open DOCX file: " + filepath;
        return false;
    }

    char header[5] = { 0 };
    file.read(header, 4);
    if (header[0] == 'P' && header[1] == 'K' && header[2] == 0x03 && header[3] == 0x04) {
        out_error = "DOCX binary (ZIP archive) detected. Please save as .txt or compile with minizip & libxml2.";
        return false;
    }

    file.close();
    return read_text_file(filepath, out_text, out_error);
}