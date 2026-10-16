/* ========================================================================== */
/* ai_engine.h                                                                */
/* ========================================================================== */
#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include "infer.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

enum class AITaskMode {
    Translate = 0,
    AutoCorrect,
    FormatLayout,
    BilingualAlign,
    ExecutiveSummary,
    Transcreate,
    ExtractGlossary
};

struct AIEngineOptions {
    AITaskMode mode = AITaskMode::Translate;
    std::string input_lang = "English";
    std::string output_lang = "French (Français)";
    std::string custom_directive;
    std::string glossary_raw;
    bool auto_correct_typos = true;
    bool layout_formatting = false;
    bool enable_continuity = false;
    bool enable_tm_cache = true;
    bool qwen3_deep_think = false;
    bool preserve_code_keys = false;
    bool is_retry = false;
    int32_t max_tokens = 2048;
};

class AIEngine {
public:
    AIEngine();
    ~AIEngine();

    AIEngine(const AIEngine&) = delete;
    AIEngine& operator=(const AIEngine&) = delete;

    bool init();
    bool load_model_async(const std::string& model_path, uint32_t requested_ctx = 0);

    bool start_task(const std::string& input_text, const AIEngineOptions& opts);

    bool start_translation_task(
        const std::string& input_lang,
        const std::string& output_lang,
        const std::string& input_text,
        const std::string& custom_directive = "",
        bool auto_correct_typos = true,
        bool layout_formatting = false,
        int32_t max_tokens = 2048
    );

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
    bool is_qwen3_loaded() const;
    bool is_using_gpu() const;
    std::string get_memory_info() const;
    std::string get_hardware_info() const;

    uint32_t get_active_ctx() const { return m_active_ctx.load(std::memory_order_relaxed); }
    std::string get_loaded_model_path() const;

    bool get_output_text_if_dirty(std::string& out_text, uint64_t& inout_rev);
    std::string get_output_text();
    void clear_output_text();

    std::string get_last_error() const;
    void get_chunk_progress(size_t& out_current, size_t& out_total, float& out_pct, size_t& out_tm_hits) const;

    void clear_tm_cache();

private:
    static bool stream_callback_v2(const char* piece, size_t len, void* user_data);
    static std::string sanitize_thinking_output(const std::string& input);
    static std::vector<std::string> split_into_chunks(const std::string& text, size_t max_chunk_chars = 3800);
    static std::vector<std::pair<std::string, std::string>> parse_glossary(const std::string& raw_glossary);
    static uint64_t fnv1a_64(const std::string& data);

    void load_tm_cache_from_disk();
    void save_tm_entry_to_disk(uint64_t hash, const std::string& translation);

    void configure_task_sampler(const AIEngineOptions& opts);
    void set_error(const std::string& err);
    void clear_error();

    std::string build_system_prompt(
        const AIEngineOptions& opts,
        const std::string& active_chunk,
        const std::vector<std::pair<std::string, std::string>>& glossary
    ) const;

    void task_thread_proc(std::string input_text, AIEngineOptions opts);

    infer_ctx* m_ctx = nullptr;
    std::atomic<bool> m_model_loaded{ false };
    std::atomic<bool> m_is_loading_model{ false };
    std::atomic<bool> m_is_qwen3{ false };
    std::atomic<uint32_t> m_active_ctx{ 4096 };
    std::atomic<bool> m_is_using_gpu{ true };
    std::atomic<uint64_t> m_model_size_bytes{ 0 };

    std::thread m_model_load_thread;
    std::string m_model_path;

    std::thread m_task_thread;
    std::atomic<bool> m_is_task_running{ false };
    mutable std::mutex m_chunk_mutex;
    size_t m_current_chunk{ 0 };
    size_t m_total_chunks{ 0 };
    size_t m_tm_cache_hits{ 0 };

    mutable std::mutex m_output_mutex;
    std::string m_output_buffer;
    std::string m_sanitized_cache;
    std::string m_current_chunk_buffer;
    uint64_t m_output_revision{ 0 };
    bool m_output_dirty{ false };

    mutable std::mutex m_tm_mutex;
    std::unordered_map<uint64_t, std::string> m_tm_cache;

    mutable std::mutex m_err_mutex;
    std::string m_last_error;
};

#endif // AI_ENGINE_H