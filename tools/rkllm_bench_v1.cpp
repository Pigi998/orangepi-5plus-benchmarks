#include <iostream>
#include <string>
#include <chrono>
#include <cstring>
#include "/tmp/rkllm_1.0.1/rkllm.h"

static int token_count = 0;
static std::chrono::steady_clock::time_point start_time;
static std::chrono::steady_clock::time_point first_token_time;
static bool first_token_received = false;

void callback(RKLLMResult* result, void* userdata, LLMCallState state) {
    if (state == LLM_RUN_NORMAL) {
        if (!first_token_received) {
            first_token_time = std::chrono::steady_clock::now();
            first_token_received = true;
        }
        if (result && result->text) {
            std::cout << result->text << std::flush;
            token_count++;
        }
    } else if (state == LLM_RUN_FINISH) {
        std::cout << "\n[Generation complete]" << std::endl;
    } else if (state == LLM_RUN_ERROR) {
        std::cerr << "\n[Error during generation]" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <model_path> [max_tokens] [context_len] [prompt]\n";
        return 1;
    }
    const char* model_path = argv[1];
    int max_tokens = (argc > 2) ? std::stoi(argv[2]) : 256;
    int context_len = (argc > 3) ? std::stoi(argv[3]) : 512;
    std::string prompt = (argc > 4) ? argv[4] : "Explain the concept of neural networks in simple terms.";

    std::cout << "========================================================\n";
    std::cout << "RKLLM v1.0.1 Hardware NPU Benchmark\n";
    std::cout << "Model: " << model_path << "\n";
    std::cout << "Max Tokens: " << max_tokens << " | Context: " << context_len << "\n";
    std::cout << "========================================================\n";

    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = model_path;
    param.max_context_len = context_len;
    param.max_new_tokens = max_tokens;
    param.num_npu_core = 3; // Use all 3 NPU cores (RK3588)

    LLMHandle handle = nullptr;
    std::cout << "-> Initializing RKLLM NPU runtime (v1.0.1)...\n";
    int ret = rkllm_init(&handle, param, callback);
    if (ret != 0) {
        std::cerr << "[!] rkllm_init failed with code: " << ret << std::endl;
        return 1;
    }
    std::cout << "[✓] NPU Model loaded successfully!\n";

    std::cout << "\n--- User Prompt ---\n" << prompt << "\n";
    std::cout << "\n--- Model Output (Hardware NPU Accelerated) ---\n";

    token_count = 0;
    first_token_received = false;
    start_time = std::chrono::steady_clock::now();

    ret = rkllm_run(handle, prompt.c_str(), nullptr);
    auto end_time = std::chrono::steady_clock::now();

    if (ret != 0) {
        std::cerr << "[!] rkllm_run failed with code: " << ret << std::endl;
        rkllm_destroy(handle);
        return 1;
    }

    double total_sec = std::chrono::duration<double>(end_time - start_time).count();
    double ttft_ms = first_token_received ? std::chrono::duration<double, std::milli>(first_token_time - start_time).count() : 0.0;
    double eval_sec = first_token_received ? std::chrono::duration<double>(end_time - first_token_time).count() : total_sec;
    double tok_per_sec = (eval_sec > 0 && token_count > 1) ? ((token_count - 1) / eval_sec) : 0.0;

    std::cout << "\n========================================================\n";
    std::cout << "NPU Performance Metrics:\n";
    std::cout << " - Generated Tokens: " << token_count << "\n";
    std::cout << " - Time to First Token (TTFT): " << ttft_ms << " ms\n";
    std::cout << " - Total Time: " << total_sec << " s\n";
    std::cout << " - Evaluation Rate: " << tok_per_sec << " tok/s\n";
    std::cout << "========================================================\n";

    rkllm_destroy(handle);
    return 0;
}
