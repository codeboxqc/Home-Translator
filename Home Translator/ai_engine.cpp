/* ========================================================================== */
/* ai_engine.cpp                                                              */
/* High-Performance Engine with Adaptive Chunking & Layout Support            */
/* ========================================================================== */
#include "ai_engine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

AIEngine::AIEngine() {}

AIEngine::~AIEngine() {
    abort();
    wait();
    if (m_model_load_thread.joinable()) {
        m_model_load_thread.join();
    }
    if (m_task_thread.joinable()) {
        m_task_thread.join();
    }
    if (m_ctx) {
        infer_free(m_ctx);
        m_ctx = nullptr;
    }
}

bool AIEngine::init() {
    if (m_ctx) return true;
    m_ctx = infer_init();
    if (!m_ctx) {
        set_error("Failed to allocate infer_ctx context instance.");
        return false;
    }
    return true;
}

bool AIEngine::load_model_async(const std::string& model_path, uint32_t requested_ctx) {
    if (!m_ctx) {
        if (!init()) return false;
    }

    if (m_is_loading_model.load() || is_generating()) {
        set_error("Engine is currently busy loading a model or generating text.");
        return false;
    }

    if (m_model_load_thread.joinable()) {
        m_model_load_thread.join();
    }

    m_is_loading_model.store(true);
    clear_error();

    m_model_load_thread = std::thread([this, model_path, requested_ctx]() {
        infer_memory_clear(m_ctx);

        bool success = infer_load_model(m_ctx, model_path.c_str(), requested_ctx);
        if (!success) {
            char err_buf[512] = { 0 };
            infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
            set_error(std::string("Model load failed: ") + err_buf);
            m_model_loaded.store(false);
        }
        else {
            infer_sampler_params params = infer_sampler_default_params();
            params.struct_size = sizeof(infer_sampler_params);
            params.temperature = 0.0f; // Greedy decoding for deterministic translation
            params.top_p = 1.0f;
            params.top_k = 1;
            params.repeat_penalty = 1.05f;

            if (!infer_set_sampler(m_ctx, &params)) {
                char err_buf[512] = { 0 };
                infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
                set_error(std::string("Failed to set sampler: ") + err_buf);
                m_model_loaded.store(false);
            }
            else {
                m_model_path = model_path;
                m_model_loaded.store(true);
            }
        }
        m_is_loading_model.store(false);
        });

    return true;
}

std::string AIEngine::build_system_prompt(
    const std::string& input_lang,
    const std::string& output_lang,
    bool is_auto_correct,
    const std::string& custom_directive,
    bool auto_correct_typos,
    bool layout_formatting
) const {
    std::string prompt;

    if (is_auto_correct || input_lang == output_lang) {
        prompt = "You are an expert orthography, grammar, typography, and typo corrector for " + input_lang + ". "
            "Fix all spelling, grammatical, syntactic, typo, and typographical errors in the provided " + input_lang + " text "
            "while strictly preserving original style and meaning.";
    }
    else {
        if (auto_correct_typos) {
            prompt = "You are an expert translator from " + input_lang + " to " + output_lang + ". "
                "Translate the provided " + input_lang + " text accurately into natural, fluent " + output_lang + ". "
                "Automatically detect and rectify any typos, spelling errors, or grammatical flaws present in the source text so the translation is polished and correct.";
        }
        else {
            prompt = "You are a direct, faithful translator from " + input_lang + " to " + output_lang + ". "
                "Translate the provided " + input_lang + " text into " + output_lang + " directly. "
                "BYPASS typo correction: preserve literal tone, slang, informalities, or intentional quirks from the source text without fixing or reinterpreting them.";
        }
    }

    if (layout_formatting) {
        prompt += "\n\nLAYOUT & EDITING INSTRUCTIONS: Format the output text with professional document/book layout. "
            "Structure into logical, readable paragraphs. Ensure proper capitalization at sentence starts. "
            "Standardize punctuation, and format dialogues cleanly (consistent dashes or quotes with line breaks per speaker). "
            "Never summarize or omit content; retain 100% of the underlying story or meaning.";
    }
    else {
        prompt += "\n\nBYPASS LAYOUT REFORMATTING: Strictly preserve the source paragraph breaks, indentation, and structure verbatim without restructuring.";
    }

    prompt += "\n\nOutput ONLY the final translated/corrected text without any introductory remarks, markdown commentary, explanations, or quotes.";

    if (!custom_directive.empty()) {
        prompt += "\n\nCRITICAL USER DIRECTIVE:\n" + custom_directive +
            "\nYou MUST strictly follow all user directives and stylistic constraints above.";
    }

    if (m_model_path.find("Qwen3") != std::string::npos) {
        prompt += " CRITICAL INSTRUCTION: Do NOT generate or output any <think> tags, reasoning steps, or internal thoughts. Output ONLY the final answer directly.";
    }

    return prompt;
}

// Zero-allocation fast-path sanitizer
std::string AIEngine::sanitize_thinking_output(const std::string& input) {
    if (input.empty()) return "";

    size_t first_tag = input.find("<think>");
    if (first_tag == std::string::npos) {
        size_t first = input.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        return (first == 0) ? input : input.substr(first);
    }

    std::string out;
    out.reserve(input.size());

    size_t i = 0;
    const size_t n = input.size();

    while (i < n) {
        size_t start_pos = input.find("<think>", i);
        if (start_pos == std::string::npos) {
            out.append(input, i, n - i);
            break;
        }

        if (start_pos > i) {
            out.append(input, i, start_pos - i);
        }

        size_t end_pos = input.find("</think>", start_pos + 7);
        if (end_pos == std::string::npos) {
            // Still generating inside thinking block
            break;
        }

        i = end_pos + 8;
    }

    size_t first_non_space = out.find_first_not_of(" \t\n\r");
    if (first_non_space == std::string::npos) {
        return "";
    }
    if (first_non_space > 0) {
        out.erase(0, first_non_space);
    }

    return out;
}

// Adaptive chunking: splits cleanly along paragraph / sentence boundaries
std::vector<std::string> AIEngine::split_into_chunks(const std::string& text, size_t max_chunk_chars) {
    std::vector<std::string> chunks;
    if (text.size() <= max_chunk_chars) {
        chunks.push_back(text);
        return chunks;
    }

    size_t start = 0;
    const size_t total_len = text.size();

    while (start < total_len) {
        if (start + max_chunk_chars >= total_len) {
            chunks.push_back(text.substr(start));
            break;
        }

        size_t target = start + max_chunk_chars;

        size_t pos = text.rfind("\n\n", target);
        if (pos == std::string::npos || pos <= start) {
            pos = text.rfind("\n", target);
        }
        if (pos == std::string::npos || pos <= start) {
            pos = text.rfind(". ", target);
            if (pos != std::string::npos && pos > start) {
                pos += 1;
            }
        }
        if (pos == std::string::npos || pos <= start) {
            pos = text.rfind(" ", target);
        }

        if (pos == std::string::npos || pos <= start) {
            pos = target;
        }
        else if (text[pos] == '\n' || text[pos] == ' ') {
            pos += 1;
        }

        // Avoid splitting in the middle of UTF-8 multibyte sequence
        while (pos > start && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
            --pos;
        }

        if (pos <= start) {
            pos = std::min(start + max_chunk_chars, total_len);
            while (pos < total_len && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
                ++pos;
            }
        }

        chunks.push_back(text.substr(start, pos - start));
        start = pos;
    }
    return chunks;
}

bool AIEngine::start_translation_task(
    const std::string& input_lang,
    const std::string& output_lang,
    const std::string& input_text,
    const std::string& custom_directive,
    bool auto_correct_typos,
    bool layout_formatting,
    int32_t max_tokens
) {
    if (!m_ctx || !m_model_loaded.load() || m_is_loading_model.load()) {
        set_error("No model loaded or model is currently loading.");
        return false;
    }

    if (input_text.empty()) {
        set_error("Input text is empty.");
        return false;
    }

    if (is_generating()) {
        set_error("Inference context is busy.");
        return false;
    }

    if (m_task_thread.joinable()) {
        m_task_thread.join();
    }

    clear_output_text();

    m_is_task_running.store(true);
    m_task_thread = std::thread(
        &AIEngine::task_thread_proc, this, input_lang, output_lang, input_text,
        custom_directive, false, auto_correct_typos, layout_formatting, max_tokens
    );

    return true;
}

bool AIEngine::start_auto_correct_task(
    const std::string& input_lang,
    const std::string& input_text,
    const std::string& custom_directive,
    bool layout_formatting,
    int32_t max_tokens
) {
    if (!m_ctx || !m_model_loaded.load() || m_is_loading_model.load()) {
        set_error("No model loaded or model is currently loading.");
        return false;
    }

    if (input_text.empty()) {
        set_error("Input text is empty.");
        return false;
    }

    if (is_generating()) {
        set_error("Inference context is busy.");
        return false;
    }

    if (m_task_thread.joinable()) {
        m_task_thread.join();
    }

    clear_output_text();

    m_is_task_running.store(true);
    m_task_thread = std::thread(
        &AIEngine::task_thread_proc, this, input_lang, input_lang, input_text,
        custom_directive, true, true, layout_formatting, max_tokens
    );

    return true;
}

void AIEngine::task_thread_proc(
    std::string input_lang,
    std::string output_lang,
    std::string input_text,
    std::string custom_directive,
    bool is_auto_correct,
    bool auto_correct_typos,
    bool layout_formatting,
    int32_t max_tokens
) {
    // 3800 chars provides optimal sentence continuity and minimizes KV cache clearing
    std::vector<std::string> chunks = split_into_chunks(input_text, 3800);

    {
        std::lock_guard<std::mutex> lock(m_chunk_mutex);
        m_current_chunk = 0;
        m_total_chunks = chunks.size();
    }

    std::string sys_prompt = build_system_prompt(
        input_lang, output_lang, is_auto_correct, custom_directive, auto_correct_typos, layout_formatting
    );

    for (size_t i = 0; i < chunks.size(); ++i) {
        if (!m_is_task_running.load()) break;

        {
            std::lock_guard<std::mutex> lock(m_chunk_mutex);
            m_current_chunk = i + 1;
        }

        infer_memory_clear(m_ctx);

        infer_chat_msg msgs[2];
        msgs[0].role = "system";
        msgs[0].content = sys_prompt.c_str();
        msgs[1].role = "user";
        msgs[1].content = chunks[i].c_str();

        bool dispatched = infer_generate_chat_async(
            m_ctx, msgs, 2, max_tokens, AIEngine::stream_callback, this
        );

        if (!dispatched) {
            char err_buf[512] = { 0 };
            infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
            set_error(std::string("Failed to dispatch async chat task: ") + err_buf);
            break;
        }

        infer_wait(m_ctx);

        if (i + 1 < chunks.size()) {
            std::lock_guard<std::mutex> lock(m_output_mutex);
            m_output_buffer.append("\n\n");
            m_output_dirty = true;
            m_output_revision++;
        }
    }

    infer_memory_clear(m_ctx);
    m_is_task_running.store(false);
}

bool AIEngine::stream_callback(const char* piece, void* user_data) {
    if (!piece || !user_data) return true;
    AIEngine* engine = static_cast<AIEngine*>(user_data);

    std::lock_guard<std::mutex> lock(engine->m_output_mutex);
    engine->m_output_buffer.append(piece);
    engine->m_output_dirty = true;
    engine->m_output_revision++;
    return true;
}

void AIEngine::abort() {
    m_is_task_running.store(false);
    if (m_ctx) {
        infer_abort(m_ctx);
    }
}

void AIEngine::wait() {
    if (m_ctx) infer_wait(m_ctx);
    if (m_task_thread.joinable()) {
        m_task_thread.join();
    }
}

bool AIEngine::is_generating() const {
    return m_is_task_running.load() || (m_ctx ? infer_is_generating(m_ctx) : false);
}

bool AIEngine::is_loading_model() const {
    return m_is_loading_model.load();
}

bool AIEngine::is_model_loaded() const {
    return m_model_loaded.load();
}

std::string AIEngine::get_loaded_model_path() const {
    return m_model_path;
}

bool AIEngine::get_output_text_if_dirty(std::string& out_text, uint64_t& inout_rev) {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    if (m_output_revision == inout_rev && !m_output_dirty) {
        return false;
    }

    if (m_output_dirty) {
        if (m_model_path.find("Qwen3") != std::string::npos || m_output_buffer.find("<think>") != std::string::npos) {
            m_sanitized_cache = sanitize_thinking_output(m_output_buffer);
        }
        else {
            m_sanitized_cache = m_output_buffer;
        }
        m_output_dirty = false;
    }

    out_text = m_sanitized_cache;
    inout_rev = m_output_revision;
    return true;
}

std::string AIEngine::get_output_text() {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    if (m_output_dirty) {
        if (m_model_path.find("Qwen3") != std::string::npos || m_output_buffer.find("<think>") != std::string::npos) {
            m_sanitized_cache = sanitize_thinking_output(m_output_buffer);
        }
        else {
            m_sanitized_cache = m_output_buffer;
        }
        m_output_dirty = false;
    }
    return m_sanitized_cache;
}

void AIEngine::clear_output_text() {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    m_output_buffer.clear();
    m_sanitized_cache.clear();
    m_output_dirty = false;
    m_output_revision++;
}

void AIEngine::get_chunk_progress(size_t& out_current, size_t& out_total, float& out_pct) const {
    std::lock_guard<std::mutex> lock(m_chunk_mutex);
    out_current = m_current_chunk;
    out_total = m_total_chunks;
    out_pct = (m_total_chunks > 0) ? (static_cast<float>(m_current_chunk) / static_cast<float>(m_total_chunks)) : 0.0f;
}

void AIEngine::set_error(const std::string& err) {
    std::lock_guard<std::mutex> lock(m_err_mutex);
    m_last_error = err;
}

void AIEngine::clear_error() {
    std::lock_guard<std::mutex> lock(m_err_mutex);
    m_last_error.clear();
}

std::string AIEngine::get_last_error() const {
    std::lock_guard<std::mutex> lock(m_err_mutex);
    return m_last_error;
}