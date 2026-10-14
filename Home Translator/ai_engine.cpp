/* ========================================================================== */
/* ai_engine.cpp                                                              */
/* Implementation with Qwen3 <think> Block Removal                            */
/* ========================================================================== */
#include "ai_engine.h"

#include <cstring>
#include <iostream>

AIEngine::AIEngine() {}

AIEngine::~AIEngine() {
    if (m_model_load_thread.joinable()) {
        m_model_load_thread.join();
    }
    if (m_ctx) {
        infer_abort(m_ctx);
        infer_wait(m_ctx);
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

    if (m_is_loading_model.load() || infer_is_generating(m_ctx)) {
        set_error("Engine is currently busy loading a model or generating text.");
        return false;
    }

    if (m_model_load_thread.joinable()) {
        m_model_load_thread.join();
    }

    m_is_loading_model.store(true);
    clear_error();

    m_model_load_thread = std::thread([this, model_path, requested_ctx]() {
        bool success = infer_load_model(m_ctx, model_path.c_str(), requested_ctx);
        if (!success) {
            char err_buf[512] = { 0 };
            infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
            set_error(std::string("Model load failed: ") + err_buf);
            m_model_loaded.store(false);
        }
        else {
            infer_sampler_params params = infer_sampler_default_params();
            params.temperature = 0.0f; // Greedy decoding
            params.top_p = 1.0f;
            params.top_k = 1;
            params.repeat_penalty = 1.0f;

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

std::string AIEngine::build_system_prompt(const std::string& input_lang, const std::string& output_lang) const {
    std::string prompt;

    if (input_lang == output_lang) {
        prompt = "You are a strict " + input_lang + " orthography, grammar, and typography corrector. "
            "Fix all spelling, grammatical, syntactic, and typographical errors in the provided " + input_lang + " text "
            "while preserving original style and meaning. Output ONLY the corrected text without any intro, commentary, or explanations.";
    }
    else {
        prompt = "You are an expert translator from " + input_lang + " to " + output_lang + ". "
            "Translate the provided " + input_lang + " text accurately into natural, fluent " + output_lang + ". "
            "Output ONLY the " + output_lang + " translation without any introductory remarks, markdown formatting, explanations, or quotes.";
    }

    // STRICT RULE FOR Qwen3: Explicitly forbid thinking/reasoning blocks
    if (m_model_path.find("Qwen3") != std::string::npos) {
        prompt += " CRITICAL INSTRUCTION: Do NOT generate or output any <think> tags, reasoning steps, or internal thoughts. Output ONLY the final answer directly.";
    }

    return prompt;
}

// Function to strip <think> ... </think> reasoning tags
std::string AIEngine::sanitize_thinking_output(const std::string& input) {
    std::string out = input;
    size_t start_pos = 0;

    while ((start_pos = out.find("<think>")) != std::string::npos) {
        size_t end_pos = out.find("</think>", start_pos);
        if (end_pos != std::string::npos) {
            // Remove the complete <think>...</think> block
            out.erase(start_pos, (end_pos + 8) - start_pos);
        }
        else {
            // If <think> tag is opened but </think> hasn't streamed yet, hide the thinking part
            out.erase(start_pos);
            break;
        }
    }

    // Trim leading whitespace if any remained after tag removal
    size_t first_non_space = out.find_first_not_of(" \t\n\r");
    if (first_non_space != std::string::npos && first_non_space > 0) {
        out.erase(0, first_non_space);
    }

    return out;
}

bool AIEngine::start_translation_task(
    const std::string& input_lang,
    const std::string& output_lang,
    const std::string& input_text,
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

    if (infer_is_generating(m_ctx)) {
        set_error("Inference context is busy.");
        return false;
    }

    clear_output_text();

    std::string sys_prompt = build_system_prompt(input_lang, output_lang);

    infer_chat_msg msgs[2];
    msgs[0].role = "system";
    msgs[0].content = sys_prompt.c_str();
    msgs[1].role = "user";
    msgs[1].content = input_text.c_str();

    bool dispatched = infer_generate_chat_async(
        m_ctx, msgs, 2, max_tokens, AIEngine::stream_callback, this
    );

    if (!dispatched) {
        char err_buf[512] = { 0 };
        infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
        set_error(std::string("Failed to dispatch async chat task: ") + err_buf);
        return false;
    }

    return true;
}

bool AIEngine::stream_callback(const char* piece, void* user_data) {
    if (!piece || !user_data) return true;
    AIEngine* engine = static_cast<AIEngine*>(user_data);

    std::lock_guard<std::mutex> lock(engine->m_output_mutex);
    engine->m_output_buffer.append(piece);
    return true;
}

void AIEngine::abort() {
    if (m_ctx) infer_abort(m_ctx);
}

void AIEngine::wait() {
    if (m_ctx) infer_wait(m_ctx);
}

bool AIEngine::is_generating() const {
    return m_ctx ? infer_is_generating(m_ctx) : false;
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

std::string AIEngine::get_output_text() {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    // Sanitize <think> blocks if Qwen3 model is loaded
    if (m_model_path.find("Qwen3") != std::string::npos) {
        return sanitize_thinking_output(m_output_buffer);
    }
    return m_output_buffer;
}

void AIEngine::clear_output_text() {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    m_output_buffer.clear();
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