
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

namespace {
    bool extract_single_epub_marker(const std::string& chunk, std::string& out_marker, std::string& out_clean_text) {
        size_t pos = 0;
        while (pos < chunk.size() && std::isspace(static_cast<unsigned char>(chunk[pos]))) pos++;
        if (pos + 3 >= chunk.size() || chunk[pos] != '[' || chunk[pos + 1] != 'E') return false;

        size_t d = pos + 2;
        while (d < chunk.size() && std::isdigit(static_cast<unsigned char>(chunk[d]))) d++;
        if (d == pos + 2 || d >= chunk.size() || chunk[d] != ']') return false;

        size_t next_e = chunk.find("\n[E", d + 1);
        if (next_e != std::string::npos) return false;

        out_marker = chunk.substr(pos, (d - pos) + 1);
        size_t text_start = d + 1;
        while (text_start < chunk.size() && (chunk[text_start] == ' ' || chunk[text_start] == '\t')) text_start++;

        out_clean_text = chunk.substr(text_start);
        while (!out_clean_text.empty() && std::isspace(static_cast<unsigned char>(out_clean_text.back()))) {
            out_clean_text.pop_back();
        }
        return !out_clean_text.empty();
    }
}

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

        mparams.kv_type_k = INFER_KV_TYPE_F16;
        mparams.kv_type_v = INFER_KV_TYPE_F16;
        mparams.allow_ctx_reduction = true;

        bool success = infer_load_model_ex(m_ctx, model_path.c_str(), &mparams);
        bool used_gpu = true;

        if (!success) {
            mparams.kv_type_k = INFER_KV_TYPE_F16;
            mparams.kv_type_v = INFER_KV_TYPE_F16;
            mparams.requested_ctx = 2048;
            mparams.n_gpu_layers = 0;
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
        params.repeat_penalty = 1.12f;
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

    case AITaskMode::Chat:
        prompt = "You are a helpful, knowledgeable, and friendly AI assistant. "
            "Answer questions accurately, conversationally, and concisely.";
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

    case AITaskMode::CodeAssistant:
        prompt = "You are an expert C and C++ software engineer and systems architect. "
            "Write clean, modern (C++20/C23 where appropriate), idiomatic, high-performance, "
            "and memory-safe code. Include clear inline comments, robust error handling, "
            "and provide complete compilable implementations.";
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
        prompt += "\n\nTranslate ONLY the human-readable text into " + opts.output_lang + ". Do NOT add explanations or notes.";
    }

    if (opts.mode != AITaskMode::CodeAssistant && opts.mode != AITaskMode::Chat && !glossary.empty() && !active_chunk.empty()) {
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

    // Set instructions depending on task mode
    if (opts.mode == AITaskMode::CodeAssistant) {
        prompt += "\n\nFormat your code inside standard markdown code blocks (e.g., ```cpp). "
            "Provide the complete solution with brief comments explaining key implementation details.";
    }
    else if (opts.mode == AITaskMode::Chat) {
        // Chat mode: Let the AI respond naturally and conversationally!
    }
    else {
        prompt += "\n\nOutput ONLY the final translation without introductory remarks, markdown commentary, explanations, or quotes.";
    }

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

    if (text.size() >= 5 && text.compare(0, 4, "[E1]") == 0) {
        size_t pos = 0;
        const size_t n = text.size();
        while (pos < n) {
            size_t open_br = text.find("[E", pos);
            if (open_br == std::string::npos) break;

            size_t next_br = std::string::npos;
            size_t search_p = open_br + 2;
            while (search_p < n) {
                size_t cand = text.find("\n[E", search_p);
                if (cand == std::string::npos) break;
                size_t d = cand + 3;
                while (d < n && std::isdigit(static_cast<unsigned char>(text[d]))) d++;
                if (d > cand + 3 && d < n && text[d] == ']') {
                    next_br = cand + 1;
                    break;
                }
                search_p = cand + 3;
            }

            std::string seg = (next_br == std::string::npos)
                ? text.substr(open_br)
                : text.substr(open_br, next_br - open_br);

            while (!seg.empty() && (seg.back() == '\n' || seg.back() == '\r' || seg.back() == ' ')) {
                seg.pop_back();
            }
            if (!seg.empty()) {
                chunks.push_back(seg);
            }
            pos = (next_br == std::string::npos) ? n : next_br;
        }

        if (!chunks.empty()) {
            return chunks;
        }
    }

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

    if (input_text.empty() && !opts.is_epub_task) {
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




    // ==========================================
   // CHAT MODE: Multi-Turn Conversation
   // ==========================================
    if (opts.mode == AITaskMode::Chat) {
        std::string sys_prompt = build_system_prompt(opts, "", {});

        std::vector<infer_chat_msg> msgs;
        msgs.reserve(opts.chat_history.size() + 2);

        // 1. System Prompt
        infer_chat_msg sys_msg;
        sys_msg.role = "system";
        sys_msg.content = sys_prompt.c_str();
        msgs.push_back(sys_msg);

        // 2. All past conversation turns (Memory)
        for (const auto& msg : opts.chat_history) {
            infer_chat_msg m;
            m.role = msg.role.c_str();
            m.content = msg.content.c_str();
            msgs.push_back(m);
        }

        // 3. New question from user
        infer_chat_msg user_msg;
        user_msg.role = "user";
        user_msg.content = input_text.c_str();
        msgs.push_back(user_msg);

        int32_t effective_max_tokens = (m_is_qwen3.load() && opts.qwen3_deep_think) ? 4096 : 2048;

        {
            std::lock_guard<std::mutex> lock(m_output_mutex);
            m_current_chunk_buffer.clear();
        }

        bool dispatched = infer_generate_chat_v2(
            m_ctx, msgs.data(), msgs.size(), effective_max_tokens, AIEngine::stream_callback_v2, this
        );

        if (!dispatched) {
            char err_buf[512] = { 0 };
            infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
            set_error(std::string("Failed to dispatch chat: ") + err_buf);
        }

        m_is_task_running.store(false);
        return; // Done! Skip all translation logic below
    }

    if (opts.is_epub_task) {
        std::string temp_dir, epub_err;
        std::vector<std::string> rel_html_files;
        if (!EpubProcessor::prepare_epub_extraction(opts.epub_input_path, temp_dir, rel_html_files, epub_err)) {
            set_error(epub_err);
            m_is_task_running.store(false);
            return;
        }

        // FIX: RAII guard guarantees temp_dir deletion on exit/abort/error
        struct TempCleanupGuard {
            std::string path;
            ~TempCleanupGuard() {
                if (!path.empty()) {
                    std::error_code ec;
                    fs::remove_all(path, ec);
                }
            }
        } temp_guard{ temp_dir };

        {
            std::lock_guard<std::mutex> lock(m_chunk_mutex);
            m_current_chunk = 0;
            m_total_chunks = rel_html_files.size();
            m_tm_cache_hits = 0;
        }

        std::string loaded_model = get_loaded_model_path();
        std::string cumulative_translated_summary;

        for (size_t file_idx = 0; file_idx < rel_html_files.size(); ++file_idx) {
            if (!m_is_task_running.load()) break;

            {
                std::lock_guard<std::mutex> lock(m_chunk_mutex);
                m_current_chunk = file_idx + 1;
            }

            fs::path full_path = fs::path(temp_dir) / fs::u8path(rel_html_files[file_idx]);
            std::vector<EpubSegment> segments;
            std::string skeleton_xml, prompt_text;

            if (!EpubProcessor::extract_single_file_segments(full_path.string(), segments, skeleton_xml, prompt_text)) {
                continue;
            }

            auto glossary = parse_glossary(opts.glossary_raw);
            uint32_t active_ctx = m_active_ctx.load(std::memory_order_relaxed);
            size_t auto_chunk_chars = std::clamp<size_t>((static_cast<size_t>(active_ctx) * 95) / 100, 1500, 14000);
            int32_t auto_max_tokens = static_cast<int32_t>(std::clamp<uint32_t>(active_ctx / 2, 1024, 8192));

            std::vector<std::string> file_chunks = split_into_chunks(prompt_text, auto_chunk_chars);
            std::string file_translated_accumulator;

            for (size_t c_idx = 0; c_idx < file_chunks.size(); ++c_idx) {
                if (!m_is_task_running.load()) break;

                std::string epub_marker;
                std::string pure_text_for_ai = file_chunks[c_idx];
                bool is_single_epub_seg = extract_single_epub_marker(file_chunks[c_idx], epub_marker, pure_text_for_ai);

                std::string sys_prompt = build_system_prompt(opts, pure_text_for_ai, glossary);
                uint64_t chunk_hash = fnv1a_64(loaded_model + "|" + sys_prompt + "|" + pure_text_for_ai);

                std::string completed_chunk;
                if (opts.enable_tm_cache && !opts.is_retry) {
                    std::lock_guard<std::mutex> lock(m_tm_mutex);
                    auto it = m_tm_cache.find(chunk_hash);
                    if (it != m_tm_cache.end()) {
                        completed_chunk = it->second;
                        m_tm_cache_hits++;
                    }
                }

                if (completed_chunk.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(m_output_mutex);
                        m_current_chunk_buffer.clear();
                    }

                    infer_chat_msg msgs[2];
                    msgs[0].role = "system";
                    msgs[0].content = sys_prompt.c_str();
                    msgs[1].role = "user";
                    msgs[1].content = pure_text_for_ai.c_str();

                    int32_t effective_max_tokens = (m_is_qwen3.load() && opts.qwen3_deep_think)
                        ? std::max(auto_max_tokens, 4096)
                        : auto_max_tokens;

                    bool dispatched = infer_generate_chat_v2(
                        m_ctx, msgs, 2, effective_max_tokens, AIEngine::stream_callback_v2, this
                    );

                    if (!dispatched) {
                        char err_buf[512] = { 0 };
                        infer_get_last_error(m_ctx, err_buf, sizeof(err_buf));
                        set_error(std::string("Failed to dispatch chat task: ") + err_buf);
                        break;
                    }

                    {
                        std::lock_guard<std::mutex> lock(m_output_mutex);
                        completed_chunk = sanitize_thinking_output(m_current_chunk_buffer);
                    }

                    if (m_is_task_running.load() && !completed_chunk.empty() && opts.enable_tm_cache) {
                        save_tm_entry_to_disk(chunk_hash, completed_chunk);
                    }
                }

                if (is_single_epub_seg) {
                    std::string chosen = completed_chunk.empty() ? pure_text_for_ai : completed_chunk;
                    file_translated_accumulator += epub_marker + " " + chosen + "\n\n";
                }
                else {
                    file_translated_accumulator += completed_chunk + "\n\n";
                }
            }

            auto file_translated_map = EpubProcessor::parse_translated_markers(file_translated_accumulator, segments.size());
            EpubProcessor::write_single_file_translation(full_path.string(), skeleton_xml, segments, file_translated_map);

            cumulative_translated_summary += "\n[Completed " + rel_html_files[file_idx] + "]\n" + file_translated_accumulator;

            {
                std::lock_guard<std::mutex> lock(m_output_mutex);
                m_output_buffer = cumulative_translated_summary;
                m_sanitized_cache = cumulative_translated_summary;
                m_output_dirty = true;
                m_output_revision++;
            }
        }

        if (m_is_task_running.load()) {
            std::string final_repack_err;
            if (!EpubProcessor::finalize_epub_repack(temp_dir, opts.epub_output_path, final_repack_err)) {
                set_error("EPUB repack failed: " + final_repack_err);
            }
        }

        m_is_task_running.store(false);
        return;
    }

    // Standard task execution...
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

    std::string clean_epub_accumulator;
    bool any_epub_seg = false;

    for (size_t i = 0; i < chunks.size(); ++i) {
        if (!m_is_task_running.load()) break;

        {
            std::lock_guard<std::mutex> lock(m_chunk_mutex);
            m_current_chunk = i + 1;
        }

        std::string epub_marker;
        std::string pure_text_for_ai = chunks[i];
        bool is_single_epub_seg = extract_single_epub_marker(chunks[i], epub_marker, pure_text_for_ai);
        if (is_single_epub_seg) any_epub_seg = true;

        std::string sys_prompt = build_system_prompt(opts, pure_text_for_ai, glossary);
        uint64_t chunk_hash = fnv1a_64(loaded_model + "|" + sys_prompt + "|" + pure_text_for_ai);

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
                    if (is_single_epub_seg) {
                        std::string entry = epub_marker + " " + cached_translation + "\n\n";
                        clean_epub_accumulator += entry;
                        m_output_buffer = clean_epub_accumulator;
                    }
                    else {
                        m_output_buffer.append(cached_translation);
                        if (i + 1 < chunks.size()) m_output_buffer.append("\n\n");
                    }
                    m_output_dirty = true;
                    m_output_revision++;
                }
                {
                    std::lock_guard<std::mutex> lock(m_chunk_mutex);
                    m_tm_cache_hits++;
                }
                prev_user_tail = pure_text_for_ai.size() > 300 ? pure_text_for_ai.substr(pure_text_for_ai.size() - 300) : pure_text_for_ai;
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
        msgs[msg_count].content = pure_text_for_ai.c_str();
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
            if (is_single_epub_seg) {
                std::string chosen = completed_chunk.empty() ? pure_text_for_ai : completed_chunk;
                clean_epub_accumulator += epub_marker + " " + chosen + "\n\n";
                m_output_buffer = clean_epub_accumulator;
                m_output_dirty = true;
                m_output_revision++;
            }
            else if (i + 1 < chunks.size()) {
                m_output_buffer.append("\n\n");
                m_output_dirty = true;
                m_output_revision++;
            }
        }

        if (m_is_task_running.load() && !completed_chunk.empty()) {
            if (opts.enable_tm_cache) {
                save_tm_entry_to_disk(chunk_hash, completed_chunk);
            }
            prev_user_tail = pure_text_for_ai.size() > 300 ? pure_text_for_ai.substr(pure_text_for_ai.size() - 300) : pure_text_for_ai;
            prev_assistant_tail = completed_chunk.size() > 300 ? completed_chunk.substr(completed_chunk.size() - 300) : completed_chunk;
        }
    }

    if (any_epub_seg) {
        std::lock_guard<std::mutex> lock(m_output_mutex);
        m_output_buffer = clean_epub_accumulator;
        m_sanitized_cache = clean_epub_accumulator;
        m_output_dirty = true;
        m_output_revision++;
    }

    m_is_task_running.store(false);
}




bool AIEngine::stream_callback_v2(const char* piece, size_t len, void* user_data) {
    if (!piece || len == 0 || !user_data) return true;
    AIEngine* engine = static_cast<AIEngine*>(user_data);

    if (!engine->m_is_task_running.load(std::memory_order_relaxed)) {
        return false;
    }

    std::string_view slice(piece, len);

    if (slice == "<|im_end|>" || slice == "<|eot_id|>" ||
        slice == "<|endoftext|>" || slice == "</s>" || slice == "<|end|>") {
        return false;
    }

    std::lock_guard<std::mutex> lock(engine->m_output_mutex);
    engine->m_output_buffer.append(piece, len);
    engine->m_current_chunk_buffer.append(piece, len);

    static const std::string_view s_stops[] = {
        "<|im_end|>", "<|eot_id|>", "<|endoftext|>", "</s>", "<|end|>"
    };
    for (const auto& stop : s_stops) {
        if (engine->m_current_chunk_buffer.size() >= stop.size()) {
            size_t pos = engine->m_current_chunk_buffer.rfind(stop);
            if (pos != std::string::npos && pos + stop.size() == engine->m_current_chunk_buffer.size()) {
                engine->m_current_chunk_buffer.erase(pos);
                if (engine->m_output_buffer.size() >= stop.size()) {
                    size_t opos = engine->m_output_buffer.rfind(stop);
                    if (opos != std::string::npos && opos + stop.size() == engine->m_output_buffer.size()) {
                        engine->m_output_buffer.erase(opos);
                    }
                }
                engine->m_output_dirty = true;
                engine->m_output_revision++;
                return false;
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
    bool is_q3 = m_is_qwen3.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(m_output_mutex);
    if (m_output_revision == inout_rev && !m_output_dirty) {
        return false;
    }

    if (m_output_dirty) {
        m_output_dirty = false;
        inout_rev = m_output_revision;

        // FIX: Avoid creating temporary raw_copy string on heap
        if (is_q3 || m_output_buffer.find("<think>") != std::string::npos) {
            out_text = sanitize_thinking_output(m_output_buffer);
        }
        else {
            out_text = m_output_buffer;
        }
        return true;
    }

    return false;
}

std::string AIEngine::get_output_text() {
    std::lock_guard<std::mutex> lock(m_output_mutex);
    if (m_output_dirty) {
        if (m_is_qwen3.load() || m_output_buffer.find("<think>") != std::string::npos) {
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