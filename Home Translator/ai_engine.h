/* ========================================================================== */
/* ai_engine.h                                                                */
/* Dynamic Language Prompts & Qwen3 Thinking-Block Suppression                */
/* ========================================================================== */
#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include "infer.h"

#include <atomic>
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
    bool start_translation_task(const std::string& input_lang, const std::string& output_lang, const std::string& input_text, int32_t max_tokens = 2048);

    void abort();
    void wait();

    bool is_generating() const;
    bool is_loading_model() const;
    bool is_model_loaded() const;
    std::string get_loaded_model_path() const;

    std::string get_output_text();
    void clear_output_text();

    std::string get_last_error() const;

private:
    static bool stream_callback(const char* piece, void* user_data);
    static std::string sanitize_thinking_output(const std::string& input);

    void set_error(const std::string& err);
    void clear_error();
    std::string build_system_prompt(const std::string& input_lang, const std::string& output_lang) const;

    infer_ctx* m_ctx = nullptr;
    std::atomic<bool> m_model_loaded{ false };
    std::atomic<bool> m_is_loading_model{ false };
    std::thread m_model_load_thread;
    std::string m_model_path;

    mutable std::mutex m_output_mutex;
    std::string m_output_buffer;

    mutable std::mutex m_err_mutex;
    std::string m_last_error;
};

#endif // AI_ENGINE_H