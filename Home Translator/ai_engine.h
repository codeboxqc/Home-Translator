/* ========================================================================== */
/* ai_engine.h                                                                */
/* Dynamic Prompts, Adaptive Chunking, Layout Mode & Fast Sanitization        */
/* ========================================================================== */
#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include "infer.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AIEngine {
public:
    AIEngine();
    ~AIEngine();

    AIEngine(const AIEngine&) = delete;
    AIEngine& operator=(const AIEngine&) = delete;

    bool init();
    bool load_model_async(const std::string& model_path, uint32_t requested_ctx = 4096);

    // Translation task with auto_correct_typos and layout_formatting toggles
    bool start_translation_task(
        const std::string& input_lang,
        const std::string& output_lang,
        const std::string& input_text,
        const std::string& custom_directive = "",
        bool auto_correct_typos = true,
        bool layout_formatting = false,
        int32_t max_tokens = 2048
    );

    // Typo / Grammar / Layout formatting task
    bool start_auto_correct_task(
        const std::string& input_lang,
        const std::string& input_text,
        const std::string& custom_directive = "",
        bool layout_formatting = false,
        int32_t max_tokens = 2048
    );

    void abort();
    void wait();

    bool is_generating() const;
    bool is_loading_model() const;
    bool is_model_loaded() const;
    std::string get_loaded_model_path() const;

    // Fast zero-allocation dirty checker for UI rendering loop
    bool get_output_text_if_dirty(std::string& out_text, uint64_t& inout_rev);
    std::string get_output_text();
    void clear_output_text();

    std::string get_last_error() const;

    // Chunk progress tracking
    void get_chunk_progress(size_t& out_current, size_t& out_total, float& out_pct) const;

private:
    static bool stream_callback(const char* piece, void* user_data);
    static std::string sanitize_thinking_output(const std::string& input);
    static std::vector<std::string> split_into_chunks(const std::string& text, size_t max_chunk_chars = 3800);

    void set_error(const std::string& err);
    void clear_error();

    std::string build_system_prompt(
        const std::string& input_lang,
        const std::string& output_lang,
        bool is_auto_correct,
        const std::string& custom_directive,
        bool auto_correct_typos,
        bool layout_formatting
    ) const;

    void task_thread_proc(
        std::string input_lang,
        std::string output_lang,
        std::string input_text,
        std::string custom_directive,
        bool is_auto_correct,
        bool auto_correct_typos,
        bool layout_formatting,
        int32_t max_tokens
    );

    infer_ctx* m_ctx = nullptr;
    std::atomic<bool> m_model_loaded{ false };
    std::atomic<bool> m_is_loading_model{ false };
    std::thread m_model_load_thread;
    std::string m_model_path;

    // Async Task & Chunk Pipeline
    std::thread m_task_thread;
    std::atomic<bool> m_is_task_running{ false };
    mutable std::mutex m_chunk_mutex;
    size_t m_current_chunk{ 0 };
    size_t m_total_chunks{ 0 };

    // Output Buffer, Version Revision & Optimized Cache
    mutable std::mutex m_output_mutex;
    std::string m_output_buffer;
    std::string m_sanitized_cache;
    uint64_t m_output_revision{ 0 };
    bool m_output_dirty{ false };

    mutable std::mutex m_err_mutex;
    std::string m_last_error;
};

#endif // AI_ENGINE_H