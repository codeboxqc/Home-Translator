/* ========================================================================== */
/* model_downloader.h                                                         */
/* Native C++ libcurl Async Model Downloader with Progress Tracking           */
/* ========================================================================== */
#ifndef MODEL_DOWNLOADER_H
#define MODEL_DOWNLOADER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ModelPreset {
    std::string name;
    std::string filename;
    std::string url;
    std::string description;
};

class ModelDownloader {
public:
    ModelDownloader();
    ~ModelDownloader();

    static const std::vector<ModelPreset>& get_presets();

    bool start_download(const ModelPreset& preset);
    void cancel_download();

    bool is_downloading() const;
    float get_progress() const;
    std::string get_status_message() const;
    std::string get_downloaded_filepath() const;
    bool is_completed() const;
    void reset_completed_flag();

    bool is_cancel_requested() const;
    void update_progress(uint64_t dlnow, uint64_t dltotal);

private:
    void download_thread_proc(ModelPreset preset);

    std::atomic<bool> m_is_downloading{ false };
    std::atomic<bool> m_cancel_requested{ false };
    std::atomic<bool> m_completed{ false };
    std::atomic<float> m_progress{ 0.0f };

    uint64_t m_bytes_downloaded{ 0 };
    uint64_t m_bytes_total{ 0 };
    std::chrono::steady_clock::time_point m_start_time;

    mutable std::mutex m_status_mutex;
    std::string m_status_message;
    std::string m_downloaded_filepath;

    std::thread m_worker_thread;
};

#endif // MODEL_DOWNLOADER_H