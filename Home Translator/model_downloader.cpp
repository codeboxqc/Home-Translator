/* ========================================================================== */
/* model_downloader.cpp                                                       */
/* High-Throughput Buffered libcurl Downloader with Throttled Speed Metric    */
/* ========================================================================== */
#include "model_downloader.h"

#include <curl/curl.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {
    static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb, void* stream) {
        std::ofstream* out = static_cast<std::ofstream*>(stream);
        size_t total_size = size * nmemb;
        out->write(static_cast<const char*>(ptr), total_size);
        if (!out->good()) {
            return 0; // Abort curl safely if disk write fails
        }
        return total_size;
    }

    static int curl_xferinfo_cb(
        void* clientp,
        curl_off_t dltotal,
        curl_off_t dlnow,
        curl_off_t ultotal,
        curl_off_t ulnow
    ) {
        (void)ultotal;
        (void)ulnow;

        ModelDownloader* downloader = static_cast<ModelDownloader*>(clientp);

        if (downloader->is_cancel_requested()) {
            return 1;
        }

        if (dltotal > 0) {
            downloader->update_progress(static_cast<uint64_t>(dlnow), static_cast<uint64_t>(dltotal));
        }

        return 0;
    }

    std::string format_bytes(uint64_t bytes) {
        constexpr double MB = 1024.0 * 1024.0;
        constexpr double GB = 1024.0 * 1024.0 * 1024.0;
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        if (bytes >= GB) {
            oss << (bytes / GB) << " GB";
        }
        else {
            oss << (bytes / MB) << " MB";
        }
        return oss.str();
    }
} // anonymous namespace

ModelDownloader::ModelDownloader() {
    curl_global_init(CURL_GLOBAL_ALL);
}

ModelDownloader::~ModelDownloader() {
    cancel_download();
    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }
    curl_global_cleanup();
}

const std::vector<ModelPreset>& ModelDownloader::get_presets() {
    static const std::vector<ModelPreset> s_presets = {
        {
            "Qwen 2.5 7B Instruct (Q4_K_M)",
            "Qwen2.5-7B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/main/Qwen2.5-7B-Instruct-Q4_K_M.gguf?download=true",
            "4.68 GB - Native support for 27 languages with exceptional translation accuracy."
        },
        {
            "Qwen 3 8B (Q4_K_M)",
            "Qwen3-8B-Q4_K_M.gguf",
            "https://huggingface.co/SandLogicTechnologies/Qwen3-GGUF/resolve/main/Qwen3-8B-Q4_K_M.gguf?download=true",
            "5.20 GB - Next-generation architecture with native support for 117 languages."
        },
        {
            "Llama 3.2 3B Instruct (Q8_0)",
            "Llama-3.2-3B-Instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q8_0.gguf?download=true",
            "3.42 GB - Fast model supporting French, English, German, Italian, Portuguese, Hindi, Spanish, Thai."
        }
    };
    return s_presets;
}

bool ModelDownloader::start_download(const ModelPreset& preset) {
    if (m_is_downloading.load()) {
        return false;
    }

    if (m_worker_thread.joinable()) {
        m_worker_thread.join();
    }

    m_is_downloading.store(true);
    m_cancel_requested.store(false);
    m_completed.store(false);
    m_progress.store(0.001f);
    m_bytes_downloaded.store(0);
    m_bytes_total.store(0);
    m_start_time = std::chrono::steady_clock::now();
    m_last_ui_update = m_start_time - std::chrono::seconds(1);

    {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_message = "Connecting to HuggingFace for " + preset.name + "...";
        m_downloaded_filepath.clear();
    }

    m_worker_thread = std::thread(&ModelDownloader::download_thread_proc, this, preset);
    return true;
}

void ModelDownloader::cancel_download() {
    if (m_is_downloading.load()) {
        m_cancel_requested.store(true);
    }
}

bool ModelDownloader::is_cancel_requested() const {
    return m_cancel_requested.load();
}

void ModelDownloader::update_progress(uint64_t dlnow, uint64_t dltotal) {
    m_bytes_downloaded.store(dlnow, std::memory_order_relaxed);
    m_bytes_total.store(dltotal, std::memory_order_relaxed);

    if (dltotal > 0) {
        float pct = static_cast<float>(static_cast<double>(dlnow) / static_cast<double>(dltotal));
        m_progress.store(pct, std::memory_order_relaxed);
    }

    auto now = std::chrono::steady_clock::now();
    if (dlnow < dltotal && std::chrono::duration<double>(now - m_last_ui_update).count() < 0.10) {
        return; // Throttle string formatting & mutex lock to 10 Hz
    }
    m_last_ui_update = now;

    double elapsed_sec = std::chrono::duration<double>(now - m_start_time).count();
    double speed_mb_s = (elapsed_sec > 0.1) ? (static_cast<double>(dlnow) / (1024.0 * 1024.0) / elapsed_sec) : 0.0;

    std::ostringstream oss;
    oss << "Downloading: " << format_bytes(dlnow) << " / " << format_bytes(dltotal)
        << " (" << std::fixed << std::setprecision(1) << speed_mb_s << " MB/s)";

    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_status_message = oss.str();
}

void ModelDownloader::download_thread_proc(ModelPreset preset) {
    fs::create_directories("models");
    fs::path target_path = fs::absolute(fs::path("models") / preset.filename);
    std::string out_path = target_path.string();

    // Allocate 2 MB disk buffer BEFORE std::ofstream so buffer outlives file stream on scope exit
    std::vector<char> disk_io_buffer(2 * 1024 * 1024);
    std::ofstream out_file;
    out_file.rdbuf()->pubsetbuf(disk_io_buffer.data(), static_cast<std::streamsize>(disk_io_buffer.size()));
    out_file.open(out_path, std::ios::binary);

    if (!out_file.is_open()) {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_message = "Error: Could not open output file for writing at " + out_path;
        m_is_downloading.store(false);
        return;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::lock_guard<std::mutex> lock(m_status_mutex);
        m_status_message = "Error: Failed to initialize libcurl handle.";
        out_file.close();
        fs::remove(out_path);
        m_is_downloading.store(false);
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, preset.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "HomeTranslator/3.0-Pro");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    out_file.close();

    if (m_cancel_requested.load() || res == CURLE_ABORTED_BY_CALLBACK) {
        {
            std::lock_guard<std::mutex> lock(m_status_mutex);
            m_status_message = "Download cancelled by user.";
        }
        fs::remove(out_path);
        m_progress.store(0.0f);
    }
    else if (res == CURLE_OK && fs::exists(out_path) && fs::file_size(out_path) > 1024 * 1024) {
        {
            std::lock_guard<std::mutex> lock(m_status_mutex);
            m_status_message = "Finished downloading: " + preset.filename;
            m_downloaded_filepath = out_path;
        }
        m_progress.store(1.0f);
        m_completed.store(true);
    }
    else {
        {
            std::lock_guard<std::mutex> lock(m_status_mutex);
            m_status_message = "Download failed: " + std::string(curl_easy_strerror(res));
        }
        fs::remove(out_path);
        m_progress.store(0.0f);
    }

    m_is_downloading.store(false);
}

bool ModelDownloader::is_downloading() const {
    return m_is_downloading.load();
}

float ModelDownloader::get_progress() const {
    return m_progress.load();
}

std::string ModelDownloader::get_status_message() const {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    return m_status_message;
}

std::string ModelDownloader::get_downloaded_filepath() const {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    return m_downloaded_filepath;
}

bool ModelDownloader::is_completed() const {
    return m_completed.load();
}

void ModelDownloader::reset_completed_flag() {
    m_completed.store(false);
}