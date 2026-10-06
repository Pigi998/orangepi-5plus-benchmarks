// RKLLM runtime 1.1.x NPU benchmark (header API: plain function-pointer callback).
// Build: g++ -O2 -o rkllm_bench_114 rkllm_bench_114.cpp -lrkllmrt
// Usage: ./rkllm_bench_114 <model.rkllm> <max_new_tokens> <max_ctx> [prompt]
// Env: RKLLM_CPUS_MASK (e.g. 0xF0 = A76 cores), RKLLM_EMBED_FLASH=1, RKLLM_RUNS=N
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "rkllm.h"

using Clock = std::chrono::steady_clock;
static int g_tokens = 0;
static bool g_first = true;
static Clock::time_point t_start, t_first, t_end;

static void callback(RKLLMResult* r, void*, LLMCallState state) {
    if (state == RKLLM_RUN_NORMAL) {
        if (g_first) { t_first = Clock::now(); g_first = false; }
        g_tokens++;
    } else if (state == RKLLM_RUN_FINISH) {
        t_end = Clock::now();
    } else if (state == RKLLM_RUN_ERROR) {
        fprintf(stderr, "RKLLM_RUN_ERROR\n");
    }
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s model max_new max_ctx [prompt]\n", argv[0]); return 1; }
    std::string prompt = argc > 4 ? argv[4] : "Explain in 3 bullet points how backpropagation works in a neural network.";
    int runs = getenv("RKLLM_RUNS") ? atoi(getenv("RKLLM_RUNS")) : 2;

    RKLLMParam p;
    memset(&p, 0, sizeof(p));
    p.model_path = argv[1];
    p.max_new_tokens = atoi(argv[2]);
    p.max_context_len = atoi(argv[3]);
    p.top_k = 1; p.top_p = 0.9f; p.temperature = 0.8f; p.repeat_penalty = 1.1f;
    p.mirostat_tau = 5.0f; p.mirostat_eta = 0.1f;
    p.skip_special_token = true; p.is_async = false;
    p.img_start = ""; p.img_end = ""; p.img_content = "";
    // 1.1.x extend_param: [4]=embed_flash [5]=enabled_cpus_num [8..11]=enabled_cpus_mask
    if (const char* m = getenv("RKLLM_CPUS_MASK")) {
        uint32_t mask = (uint32_t)strtoul(m, nullptr, 0);
        p.extend_param.reserved[1] = (uint8_t)__builtin_popcount(mask);
        memcpy(p.extend_param.reserved + 4, &mask, sizeof(mask));
    }
    if (const char* e = getenv("RKLLM_EMBED_FLASH")) p.extend_param.reserved[0] = (uint8_t)atoi(e);

    LLMHandle h = nullptr;
    auto i0 = Clock::now();
    int ret = rkllm_init(&h, &p, callback);
    if (ret != 0) { fprintf(stderr, "rkllm_init failed: %d\n", ret); return 1; }
    printf("init_ms=%.0f\n", std::chrono::duration<double, std::milli>(Clock::now() - i0).count());

    RKLLMInput in; memset(&in, 0, sizeof(in));
    in.input_type = RKLLM_INPUT_PROMPT;
    in.prompt_input = prompt.c_str();
    RKLLMInferParam ip; memset(&ip, 0, sizeof(ip));
    ip.mode = RKLLM_INFER_GENERATE;

    for (int i = 0; i < runs; i++) {
        g_tokens = 0; g_first = true;
        t_start = Clock::now();
        ret = rkllm_run(h, &in, &ip, nullptr);
        if (ret != 0) { fprintf(stderr, "rkllm_run failed: %d\n", ret); break; }
        double ttft = std::chrono::duration<double, std::milli>(t_first - t_start).count();
        double gen = std::chrono::duration<double>(t_end - t_first).count();
        double tps = gen > 0 ? (g_tokens - 1) / gen : 0;
        FILE* f = fopen("/sys/class/thermal/thermal_zone0/temp", "r"); int t = 0; if (f) { fscanf(f, "%d", &t); fclose(f); }
        printf("run%d tokens=%d ttft_ms=%.0f decode_tok_s=%.2f temp=%.1fC\n", i, g_tokens, ttft, tps, t / 1000.0);
        fflush(stdout);
    }
    rkllm_destroy(h);
    return 0;
}
