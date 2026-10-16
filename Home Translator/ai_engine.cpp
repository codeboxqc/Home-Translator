/* ========================================================================== */
/* ai_engine.cpp                                                              */
/* ========================================================================== */
#include "ai_engine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

AIEngine::AIEngine() {
    load_tm_cache_from_disk();
}

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

uint64_t AIEngine::fnv1a_64(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void AIEngine::load_tm_cache_from_disk() {
    std::lock_guard<std::mutex> lock(m_tm_mutex);
    std::ifstream in("models/tm_cache.bin", std::ios::binary);
    if (!in.is_open()) return;

    while (in.good()) {
        uint64_t hash = 0;
        uint32_t len = 0;
        if (!in.read(reinterpret_cast<char*>(&hash), sizeof(hash))) break;
        if (!in.read(reinterpret_cast<char*>(&len), sizeof(len)) || len > 1048576) break;
        std::string val(len, '\0');
        if (!in.read(&val[0], len)) break;
        m_tm_cache[hash] = std::move(val);
    }
}

void AIEngine::save_tm_entry_to_disk(uint64_t hash, const std::string& translation) {
    if (translation.empty() || translation.size() > 1048576) return;
    std::lock_guard<std::mutex> lock(m_tm_mutex);
    m_tm_cache[hash] = translation;

    fs::create_directories("models");
    std::ofstream out("models/tm_cache.bin", std::ios::binary | std::ios::app);
    if (out.is_open()) {
        uint32_t len = static_cast<uint32_t>(translation.size());
        out.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(translation.data(), len);
    }
}

void AIEngine::clear_tm_cache() {
    std::lock_guard<std::mutex> lock(m_tm_mutex);
    m_tm_cache.clear();
    std::error_code ec;
    fs::remove("models/tm_cache.bin", ec);
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

    uint32_t safe_ctx = (requested_ctx == 0) ? 0u : std::clamp(requested_ctx, 512u, 131072u);

    m_model_load_thread = std::thread([this, model_path, safe_ctx]() {
        (void)infer_memory_clear(m_ctx);

        std::error_code ec_size;
        uint64_t fsize = fs::file_size(model_path, ec_size);
        m_model_size_bytes.store(fsize, std::memory_order_relaxed);

        infer_model_params mparams = infer_model_default_params();
        mparams.struct_size = sizeof(infer_model_params);
        mparams.requested_ctx = safe_ctx;
        mparams.n_gpu_layers = 999;
        mparams.use_mmap = true;
        mparams.flash_attn = true;

        mparams.kv_type_k = INFER_KV_TYPE_Q4_0;
        mparams.kv_type_v = INFER_KV_TYPE_Q4_0;
        mparams.allow_ctx_reduction = true;

        bool success = infer_load_model_ex(m_ctx, model_path.c_str(), &mparams);
        bool used_gpu = true;

        if (!success) {
            // Fallback for CPU / Low-VRAM
            mparams.kv_type_k = INFER_KV_TYPE_Q4_0;
            mparams.kv_type_v = INFER_KV_TYPE_Q4_0;
            mparams.requested_ctx = 2048;
            mparams.n_gpu_layers = 0; // CPU fallback
            success = infer_load_model_ex(m_ctx, model_path.c_str(), &mparams);
            used_gpu = false;
        }

        if (success && mparams.n_gpu_layers == 0) {
            used_gpu = false;
        }

        m_is_using_gpu.store(used_gpu, std::memory_order_relaxed);

        if (!success) {
            char err_buf[512] = { 0 };
            infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
            set_error(std::string("Model load failed: ") + err_buf);
            m_model_loaded.store(false);
        }
        else {
            infer_sampler_params params = infer_sampler_default_params();
            params.struct_size = sizeof(infer_sampler_params);
            params.temperature = 0.0f;
            params.top_p = 1.0f;
            params.top_k = 0;
            params.min_p = 0.0f;
            params.repeat_penalty = 1.05f;

            if (!infer_set_sampler(m_ctx, &params)) {
                char err_buf[512] = { 0 };
                infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
                set_error(std::string("Failed to set sampler: ") + err_buf);
                m_model_loaded.store(false);
            }
            else {
                // Configure standard stop sequences across all supported models
                static const char* const s_default_stops[] = {
                    "<|im_end|>", "<|endoftext|>", "<|eot_id|>", "</s>", "<|end|>", "<|user|>"
                };
                (void)infer_set_stop_sequences(m_ctx, s_default_stops, sizeof(s_default_stops) / sizeof(s_default_stops[0]));

                infer_model_info info{};
                info.struct_size = sizeof(infer_model_info);
                if (infer_get_model_info(m_ctx, &info) && info.n_ctx_active > 0) {
                    m_active_ctx.store(info.n_ctx_active, std::memory_order_relaxed);
                }
                {
                    std::lock_guard<std::mutex> lock(m_output_mutex);
                    m_model_path = model_path;
                    m_is_qwen3.store(model_path.find("Qwen3") != std::string::npos);
                }
                m_model_loaded.store(true);
            }
        }
        m_is_loading_model.store(false);
        });

    return true;
}

void AIEngine::configure_task_sampler(const AIEngineOptions& opts) {
    if (!m_ctx) return;

    infer_sampler_params params = infer_sampler_default_params();
    params.struct_size = sizeof(infer_sampler_params);

    if (!opts.is_retry && !opts.qwen3_deep_think) {
        params.temperature = 0.0f;
        params.top_p = 1.0f;
        params.top_k = 0;
        params.min_p = 0.0f;
        params.repeat_penalty = 1.05f;
    }
    else {
        params.temperature = opts.is_retry ? 0.40f : 0.60f;
        params.top_p = 0.90f;
        params.top_k = 30;
        params.min_p = 0.05f;
        params.repeat_penalty = 1.08f;
    }

    (void)infer_set_sampler(m_ctx, &params);
}

std::vector<std::pair<std::string, std::string>> AIEngine::parse_glossary(const std::string& raw_glossary) {
    std::vector<std::pair<std::string, std::string>> entries;
    if (raw_glossary.empty()) return entries;

    std::istringstream iss(raw_glossary);
    std::string line;
    while (std::getline(iss, line)) {
        size_t sep = line.find('=');
        if (sep == std::string::npos) sep = line.find("->");
        if (sep != std::string::npos) {
            std::string src = line.substr(0, sep);
            std::string dst = line.substr(sep + (line[sep] == '-' ? 2 : 1));
            auto trim = [](std::string& s) {
                size_t first = s.find_first_not_of(" \t\r\n");
                size_t last = s.find_last_not_of(" \t\r\n");
                s = (first == std::string::npos) ? "" : s.substr(first, last - first + 1);
                };
            trim(src);
            trim(dst);
            if (!src.empty() && !dst.empty()) {
                entries.emplace_back(src, dst);
            }
        }
    }
    return entries;
}

std::string AIEngine::build_system_prompt(
    const AIEngineOptions& opts,
    const std::string& active_chunk,
    const std::vector<std::pair<std::string, std::string>>& glossary
) const {
    std::string prompt;

    switch (opts.mode) {
    case AITaskMode::AutoCorrect:
        prompt = "You are an expert orthography, grammar, typography, and typo corrector for " + opts.input_lang + ". "
            "Fix all spelling, grammatical, syntactic, typo, and typographical errors in the provided " + opts.input_lang + " text "
            "while strictly preserving original style and meaning.";
        break;

    case AITaskMode::FormatLayout:
        prompt = "You are a professional book typesetter and literary editor for " + opts.input_lang + ". "
            "Restructure the provided text into clean, logical paragraphs, standardize dialogue punctuation, "
            "and fix spelling/grammar errors while preserving 100% of the story and voice.";
        break;

    case AITaskMode::BilingualAlign:
        prompt = "You are an expert bilingual translator from " + opts.input_lang + " to " + opts.output_lang + ". "
            "For every paragraph in the source text, output the original " + opts.input_lang + " paragraph first, followed immediately on the next line by its "
            "fluent, accurate " + opts.output_lang + " translation, separated by a blank line.";
        break;

    case AITaskMode::ExecutiveSummary:
        prompt = "You are an executive analyst. Analyze the provided " + opts.input_lang + " text and produce a structured Executive Summary written in " + opts.output_lang + ". "
            "Include: 1) Core Thesis / Overview, 2) Key Takeaways (bullet points), and 3) Critical Details or Action Items.";
        break;

    case AITaskMode::Transcreate:
        prompt = "You are a master literary and cultural transcreator from " + opts.input_lang + " into " + opts.output_lang + ". "
            "Adapt idioms, metaphors, humor, and emotional resonance so the text reads as if originally written by a native " + opts.output_lang + " author.";
        break;

    case AITaskMode::ExtractGlossary:
        prompt = "You are a terminology extraction specialist. Scan the provided " + opts.input_lang + " text and extract all proper nouns, character names, locations, "
            "and recurring technical terms. Output ONLY a clean glossary list formatted strictly as: Source Term = " + opts.output_lang + " Equivalent (one per line).";
        break;

    case AITaskMode::Translate:
    default:
        if (opts.input_lang == opts.output_lang) {
            prompt = "You are an expert orthography, grammar, and typo corrector for " + opts.input_lang + ". "
                "Fix all errors while strictly preserving original meaning.";
        }
        else if (opts.auto_correct_typos) {
            prompt = "You are an expert translator from " + opts.input_lang + " to " + opts.output_lang + ". "
                "Translate the provided " + opts.input_lang + " text accurately into natural, fluent " + opts.output_lang + ". "
                "Automatically detect and rectify any typos or grammatical flaws present in the source text.";
        }
        else {
            prompt = "You are a direct, faithful translator from " + opts.input_lang + " to " + opts.output_lang + ". "
                "Translate directly into " + opts.output_lang + ". BYPASS typo correction: preserve literal tone, slang, and informalities.";
        }
        break;
    }

    if (opts.layout_formatting && opts.mode == AITaskMode::Translate) {
        prompt += "\n\nLAYOUT & EDITING INSTRUCTIONS: Format the output text with professional book layout. "
            "Structure into logical, readable paragraphs. Standardize punctuation and dialogue formatting. Retain 100% of the content.";
    }
    else if (opts.mode == AITaskMode::Translate || opts.mode == AITaskMode::AutoCorrect) {
        prompt += "\n\nBYPASS LAYOUT REFORMATTING: Strictly preserve the source paragraph breaks, line numbers, and structure verbatim.";
    }

    if (opts.preserve_code_keys) {
        prompt += "\n\nLOCALIZATION & SUBTITLE LOCK: Preserve all [Cue #] markers, JSON keys, XML/HTML tags, and variable placeholders untouched. Translate ONLY the human-readable values.";
    }

    if (!glossary.empty() && !active_chunk.empty()) {
        std::string matched_rules;
        for (const auto& kv : glossary) {
            auto it = std::search(
                active_chunk.begin(), active_chunk.end(),
                kv.first.begin(), kv.first.end(),
                [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }
            );
            if (it != active_chunk.end()) {
                matched_rules += "- \"" + kv.first + "\" -> \"" + kv.second + "\"\n";
            }
        }
        if (!matched_rules.empty()) {
            prompt += "\n\nMANDATORY TERMINOLOGY GLOSSARY:\n" + matched_rules;
        }
    }

    prompt += "\n\nOutput ONLY the final result without introductory remarks, markdown commentary, explanations, or quotes.";

    if (!opts.custom_directive.empty()) {
        prompt += "\n\nCRITICAL USER DIRECTIVE:\n" + opts.custom_directive +
            "\nYou MUST strictly follow all user directives above.";
    }

    if (m_is_qwen3.load()) {
        if (opts.qwen3_deep_think) {
            prompt += "\n/think";
        }
        else {
            prompt += " CRITICAL INSTRUCTION: Do NOT generate or output any <think> tags, reasoning steps, or internal thoughts. Output ONLY the final answer directly.\n/no_think";
        }
    }

    return prompt;
}

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
        else {
            while (pos < total_len && (text[pos] == '\n' || text[pos] == '\r' || text[pos] == ' ')) {
                pos += 1;
            }
        }

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

bool AIEngine::start_task(const std::string& input_text, const AIEngineOptions& opts) {
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
    m_task_thread = std::thread(&AIEngine::task_thread_proc, this, input_text, opts);
    return true;
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
    AIEngineOptions opts;
    opts.mode = AITaskMode::Translate;
    opts.input_lang = input_lang;
    opts.output_lang = output_lang;
    opts.custom_directive = custom_directive;
    opts.auto_correct_typos = auto_correct_typos;
    opts.layout_formatting = layout_formatting;
    opts.max_tokens = max_tokens;
    return start_task(input_text, opts);
}

bool AIEngine::start_auto_correct_task(
    const std::string& input_lang,
    const std::string& input_text,
    const std::string& custom_directive,
    bool layout_formatting,
    int32_t max_tokens
) {
    AIEngineOptions opts;
    opts.mode = layout_formatting ? AITaskMode::FormatLayout : AITaskMode::AutoCorrect;
    opts.input_lang = input_lang;
    opts.output_lang = input_lang;
    opts.custom_directive = custom_directive;
    opts.auto_correct_typos = true;
    opts.layout_formatting = layout_formatting;
    opts.max_tokens = max_tokens;
    return start_task(input_text, opts);
}

void AIEngine::task_thread_proc(std::string input_text, AIEngineOptions opts) {
    configure_task_sampler(opts);

    uint32_t active_ctx = m_active_ctx.load(std::memory_order_relaxed);
    size_t auto_chunk_chars = std::clamp<size_t>((static_cast<size_t>(active_ctx) * 95) / 100, 1500, 14000);
    int32_t auto_max_tokens = static_cast<int32_t>(std::clamp<uint32_t>(active_ctx / 2, 1024, 8192));

    std::vector<std::string> chunks = split_into_chunks(input_text, auto_chunk_chars);
    auto glossary = parse_glossary(opts.glossary_raw);

    {
        std::lock_guard<std::mutex> lock(m_chunk_mutex);
        m_current_chunk = 0;
        m_total_chunks = chunks.size();
        m_tm_cache_hits = 0;
    }

    std::string prev_user_tail;
    std::string prev_assistant_tail;
    std::string loaded_model = get_loaded_model_path();

    for (size_t i = 0; i < chunks.size(); ++i) {
        if (!m_is_task_running.load()) break;

        {
            std::lock_guard<std::mutex> lock(m_chunk_mutex);
            m_current_chunk = i + 1;
        }

        std::string sys_prompt = build_system_prompt(opts, chunks[i], glossary);
        uint64_t chunk_hash = fnv1a_64(loaded_model + "|" + sys_prompt + "|" + chunks[i]);

        if (opts.enable_tm_cache && !opts.is_retry) {
            std::string cached_translation;
            {
                std::lock_guard<std::mutex> lock(m_tm_mutex);
                auto it = m_tm_cache.find(chunk_hash);
                if (it != m_tm_cache.end()) {
                    cached_translation = it->second;
                }
            }
            if (!cached_translation.empty()) {
                {
                    std::lock_guard<std::mutex> lock(m_output_mutex);
                    m_output_buffer.append(cached_translation);
                    if (i + 1 < chunks.size()) m_output_buffer.append("\n\n");
                    m_output_dirty = true;
                    m_output_revision++;
                }
                {
                    std::lock_guard<std::mutex> lock(m_chunk_mutex);
                    m_tm_cache_hits++;
                }
                prev_user_tail = chunks[i].size() > 300 ? chunks[i].substr(chunks[i].size() - 300) : chunks[i];
                prev_assistant_tail = cached_translation.size() > 300 ? cached_translation.substr(cached_translation.size() - 300) : cached_translation;
                continue;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_output_mutex);
            m_current_chunk_buffer.clear();
        }

        infer_chat_msg msgs[4];
        size_t msg_count = 0;

        msgs[msg_count].role = "system";
        msgs[msg_count].content = sys_prompt.c_str();
        msg_count++;

        if (opts.enable_continuity && i > 0 && !prev_user_tail.empty() && !prev_assistant_tail.empty()) {
            msgs[msg_count].role = "user";
            msgs[msg_count].content = prev_user_tail.c_str();
            msg_count++;

            msgs[msg_count].role = "assistant";
            msgs[msg_count].content = prev_assistant_tail.c_str();
            msg_count++;
        }

        msgs[msg_count].role = "user";
        msgs[msg_count].content = chunks[i].c_str();
        msg_count++;

        int32_t effective_max_tokens = (m_is_qwen3.load() && opts.qwen3_deep_think)
            ? std::max(auto_max_tokens, 4096)
            : auto_max_tokens;

        bool dispatched = infer_generate_chat_v2(
            m_ctx, msgs, msg_count, effective_max_tokens, AIEngine::stream_callback_v2, this
        );

        if (!dispatched) {
            char err_buf[512] = { 0 };
            infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
            set_error(std::string("Failed to dispatch chat task: ") + err_buf);
            break;
        }

        std::string completed_chunk;
        {
            std::lock_guard<std::mutex> lock(m_output_mutex);
            completed_chunk = sanitize_thinking_output(m_current_chunk_buffer);
            if (i + 1 < chunks.size()) {
                m_output_buffer.append("\n\n");
                m_output_dirty = true;
                m_output_revision++;
            }
        }

        if (m_is_task_running.load() && !completed_chunk.empty()) {
            if (opts.enable_tm_cache) {
                save_tm_entry_to_disk(chunk_hash, completed_chunk);
            }
            prev_user_tail = chunks[i].size() > 300 ? chunks[i].substr(chunks[i].size() - 300) : chunks[i];
            prev_assistant_tail = completed_chunk.size() > 300 ? completed_chunk.substr(completed_chunk.size() - 300) : completed_chunk;
        }
    }

    m_is_task_running.store(false);
}

bool AIEngine::stream_callback_v2(const char* piece, size_t len, void* user_data) {
    if (!piece || len == 0 || !user_data) return true;
    AIEngine* engine = static_cast<AIEngine*>(user_data);

    // Abort decode loop immediately if task cancellation was signaled
    if (!engine->m_is_task_running.load(std::memory_order_relaxed)) {
        return false;
    }

    std::string_view slice(piece, len);

    // Immediate terminator check: halt decoding the instant the model hits an EOG token
    if (slice == "<|im_end|>" || slice == "<|eot_id|>" ||
        slice == "<|endoftext|>" || slice == "</s>" || slice == "<|end|>") {
        return false;
    }

    std::lock_guard<std::mutex> lock(engine->m_output_mutex);
    engine->m_output_buffer.append(piece, len);
    engine->m_current_chunk_buffer.append(piece, len);

    // Scan trailing boundary of buffer for partial or concatenated stop sequences
    static const std::string_view s_stops[] = {
        "<|im_end|>", "<|eot_id|>", "<|endoftext|>", "</s>", "<|end|>"
    };
    for (const auto& stop : s_stops) {
        if (engine->m_output_buffer.size() >= stop.size()) {
            size_t pos = engine->m_output_buffer.rfind(stop);
            if (pos != std::string::npos && pos + stop.size() == engine->m_output_buffer.size()) {
                engine->m_output_buffer.erase(pos);
                engine->m_output_dirty = true;
                engine->m_output_revision++;
                return false; // Stop decoding immediately
            }
        }
    }

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

bool AIEngine::is_qwen3_loaded() const {
    return m_is_qwen3.load();
}

bool AIEngine::is_using_gpu() const {
    return m_is_using_gpu.load(std::memory_order_relaxed);
}

std::string AIEngine::get_memory_info() const {
    uint64_t bytes = m_model_size_bytes.load(std::memory_order_relaxed);
    uint32_t ctx = m_active_ctx.load(std::memory_order_relaxed);

    double model_gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    double kv_mb = static_cast<double>(ctx) * 0.035;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (bytes >= 1024 * 1024 * 1024) {
        oss << "Model VRAM/RAM: " << model_gb << " GB";
    }
    else if (bytes > 0) {
        oss << "Model VRAM/RAM: " << (bytes / (1024.0 * 1024.0)) << " MB";
    }
    else {
        oss << "Model VRAM/RAM: Active";
    }
    oss << " | Context Cache: " << ctx << " tokens (~" << static_cast<int>(kv_mb) << " MB)";
    return oss.str();
}

std::string AIEngine::get_hardware_info() const {
    if (m_is_using_gpu.load(std::memory_order_relaxed)) {
        return "Backend: GPU Offload Active (Flash Attention Enabled)";
    }
    else {
        return "Backend: CPU Multi-Threading Mode (No GPU Offload)";
    }
}

std::string AIEngine::get_loaded_model_path() const {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    return m_model_path;
}

bool AIEngine::get_output_text_if_dirty(std::string& out_text, uint64_t& inout_rev) {
    std::string raw_copy;
    bool needs_update = false;
    bool is_q3 = m_is_qwen3.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(m_output_mutex);
        if (m_output_revision == inout_rev && !m_output_dirty) {
            return false;
        }

        if (m_output_dirty) {
            raw_copy = m_output_buffer;
            m_output_dirty = false;
            needs_update = true;
            inout_rev = m_output_revision;
        }
    }

    if (needs_update) {
        if (is_q3 || raw_copy.compare(0, 7, "<think>") == 0) {
            out_text = sanitize_thinking_output(raw_copy);
        }
        else {
            out_text = std::move(raw_copy);
        }
        return true;
    }

    return false;
}

std::string AIEngine::get_output_text() {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    if (m_output_dirty) {
        if (m_is_qwen3.load() || m_output_buffer.compare(0, 7, "<think>") == 0) {
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
    m_current_chunk_buffer.clear();
    if (m_output_buffer.capacity() > 1048576) m_output_buffer.shrink_to_fit();
    if (m_sanitized_cache.capacity() > 1048576) m_sanitized_cache.shrink_to_fit();
    if (m_current_chunk_buffer.capacity() > 1048576) m_current_chunk_buffer.shrink_to_fit();
    m_output_dirty = false;
    m_output_revision++;
}

void AIEngine::get_chunk_progress(size_t& out_current, size_t& out_total, float& out_pct, size_t& out_tm_hits) const {
    std::lock_guard<std::mutex> lock(m_chunk_mutex);
    out_current = m_current_chunk;
    out_total = m_total_chunks;
    out_tm_hits = m_tm_cache_hits;
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