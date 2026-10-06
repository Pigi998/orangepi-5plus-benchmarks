#include <iostream>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <csignal>
#include "rkllm.h"

using namespace std;

LLMHandle llmHandle = nullptr;
static int g_token_count = 0;
static bool g_first_token = true;
static chrono::high_resolution_clock::time_point g_start_time;
static chrono::high_resolution_clock::time_point g_first_token_time;
static chrono::high_resolution_clock::time_point g_end_time;
static bool g_finished = false;

int callback(RKLLMResult* result, void* userdata, LLMCallState state)
{
    if (state == RKLLM_RUN_NORMAL) {
        if (result && result->text) {
            if (g_first_token) {
                g_first_token_time = chrono::high_resolution_clock::now();
                g_first_token = false;
            }
            g_token_count++;
            cout << result->text << flush;
        }
    } else if (state == RKLLM_RUN_FINISH) {
        g_end_time = chrono::high_resolution_clock::now();
        g_finished = true;
    } else if (state == RKLLM_RUN_ERROR) {
        cerr << "\n[!] RKLLM Error during inference\n";
        g_finished = true;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        cerr << "Usage: " << argv[0] << " <model_path> <max_new_tokens> <max_context_len> [prompt]\n";
        return 1;
    }

    const char* model_path = argv[1];
    int max_new_tokens = atoi(argv[2]);
    int max_context_len = atoi(argv[3]);
    string prompt = (argc >= 5) ? argv[4] : "Explain in 3 distinct bullet points how a neural network learns using backpropagation.";

    cout << "========================================================\n";
    cout << "RKLLM Hardware NPU Benchmark\n";
    cout << "Model: " << model_path << "\n";
    cout << "Max Tokens: " << max_new_tokens << " | Context: " << max_context_len << "\n";
    cout << "========================================================\n";

    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = model_path;
    param.max_new_tokens = max_new_tokens;
    param.max_context_len = max_context_len;
    param.top_k = 1;
    param.top_p = 0.95;
    param.temperature = 0.2;
    param.skip_special_token = true;

    RKLLMCallback rkllm_callback = {};
    rkllm_callback.result_callback = callback;

    cout << "-> Initializing RKLLM NPU runtime...\n";
    auto init_start = chrono::high_resolution_clock::now();
    int ret = rkllm_init(&llmHandle, &param, &rkllm_callback);
    auto init_end = chrono::high_resolution_clock::now();
    double init_duration_ms = chrono::duration<double, milli>(init_end - init_start).count();

    if (ret != 0) {
        cerr << "[!] rkllm_init failed with error code: " << ret << "\n";
        return 1;
    }
    cout << "RKLLM NPU initialized in " << init_duration_ms << " ms\n\n";

    RKLLMInput rkllm_input;
    memset(&rkllm_input, 0, sizeof(RKLLMInput));
    rkllm_input.input_type = RKLLM_INPUT_PROMPT;
    rkllm_input.prompt_input = prompt.c_str();

    RKLLMInferParam infer_param;
    memset(&infer_param, 0, sizeof(RKLLMInferParam));
    infer_param.mode = RKLLM_INFER_GENERATE;

    cout << "Prompt: " << prompt << "\n";
    cout << "--- Response ---\n";

    g_token_count = 0;
    g_first_token = true;
    g_finished = false;
    g_start_time = chrono::high_resolution_clock::now();

    ret = rkllm_run(llmHandle, &rkllm_input, &infer_param, nullptr);
    if (ret != 0) {
        cerr << "\n[!] rkllm_run failed with code: " << ret << "\n";
        rkllm_destroy(llmHandle);
        return 1;
    }

    double ttft_ms = chrono::duration<double, milli>(g_first_token_time - g_start_time).count();
    double gen_time_s = chrono::duration<double>(g_end_time - g_first_token_time).count();
    double total_time_s = chrono::duration<double>(g_end_time - g_start_time).count();
    double tok_per_sec = (gen_time_s > 0) ? (g_token_count / gen_time_s) : 0.0;

    cout << "\n--------------------------------------------------------\n";
    cout << "NPU Benchmark Summary:\n";
    cout << "   Total Tokens: " << g_token_count << "\n";
    cout << "   Time To First Token (TTFT): " << ttft_ms << " ms\n";
    cout << "   Generation Time: " << gen_time_s << " s\n";
    cout << "   Generation Speed: " << tok_per_sec << " tok/s\n";
    cout << "   Total Wall Time: " << total_time_s << " s\n";
    cout << "--------------------------------------------------------\n";

    cout << "{\"backend\": \"RKLLM (NPU)\", \"model\": \"" << model_path 
         << "\", \"eval_tok_per_sec\": " << tok_per_sec 
         << ", \"ttft_ms\": " << ttft_ms 
         << ", \"tokens\": " << g_token_count << "}\n";

    rkllm_destroy(llmHandle);
    return 0;
}
