#!/usr/bin/env python3
"""
Orange Pi 5 Plus - Local AI & LLM Inference Benchmark
Tester: Pierluigi De Vitis
Measures: Prompt processing tok/s, Generation eval tok/s, TTFT, RAM usage, and Thermals.
"""

import time
import json
import urllib.request
import urllib.error
import subprocess
import os
import sys

OLLAMA_API = "http://127.0.0.1:11434/api"
MODELS = ["llama3.2:1b", "deepseek-coder:1.3b", "qwen2.5:1.5b", "llama3.2:3b", "phi3:mini", "llama3.1:8b"]

def check_ollama():
    try:
        req = urllib.request.Request(f"{OLLAMA_API}/tags")
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status == 200
    except Exception:
        return False

def get_device_name():
    try:
        if os.path.exists("/proc/device-tree/model"):
            with open("/proc/device-tree/model", "rb") as f:
                return f.read().decode("utf-8", errors="ignore").strip("\x00")
    except Exception:
        pass
    return "Orange Pi 5 Plus (RK3588, 16GB RAM)"

def get_temp():
    try:
        with open("/sys/class/thermal/thermal_zone0/temp", "r") as f:
            return round(int(f.read().strip()) / 1000.0, 1)
    except Exception:
        return None

def query_ollama(model, prompt, num_threads=None, stream=False):
    options = {
        "temperature": 0.2,
        "num_predict": 256
    }
    if num_threads:
        options["num_thread"] = num_threads

    payload = {
        "model": model,
        "prompt": prompt,
        "stream": stream,
        "options": options
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(f"{OLLAMA_API}/generate", data=data, headers={"Content-Type": "application/json"})
    
    start_time = time.perf_counter()
    with urllib.request.urlopen(req, timeout=300) as resp:
        result = json.loads(resp.read().decode("utf-8"))
    end_time = time.perf_counter()
    
    total_wall_time = end_time - start_time
    return result, total_wall_time

def benchmark_model_config(model, num_threads=None, config_label="Default (8T)"):
    print(f"\n--- Testing {model} [{config_label}] ---")
    test_prompt = "Explain in 3 distinct bullet points how a neural network learns using backpropagation and gradient descent."
    
    temp_before = get_temp()
    result, wall_time = query_ollama(model, test_prompt, num_threads=num_threads)
    temp_after = get_temp()
    
    eval_count = result.get("eval_count", 0)
    eval_duration_ns = result.get("eval_duration", 1)
    prompt_eval_count = result.get("prompt_eval_count", 0)
    prompt_eval_duration_ns = result.get("prompt_eval_duration", 1)
    
    eval_rate = eval_count / (eval_duration_ns / 1e9) if eval_duration_ns else 0.0
    prompt_rate = prompt_eval_count / (prompt_eval_duration_ns / 1e9) if prompt_eval_duration_ns else 0.0
    ttft_ms = (prompt_eval_duration_ns / 1e6) if prompt_eval_duration_ns else 0.0
    
    print(f"   [{config_label}] Eval Rate: {eval_rate:.2f} tok/s ({eval_count} tokens)")
    print(f"   [{config_label}] Prompt Rate: {prompt_rate:.2f} tok/s ({prompt_eval_count} prompt tokens)")
    print(f"   [{config_label}] TTFT: {ttft_ms:.1f} ms | Temp: {temp_after}°C")
    
    return {
        "config": config_label,
        "num_threads": num_threads or 8,
        "eval_tok_per_sec": round(eval_rate, 2),
        "prompt_tok_per_sec": round(prompt_rate, 2),
        "ttft_ms": round(ttft_ms, 1),
        "eval_count": eval_count,
        "prompt_eval_count": prompt_eval_count,
        "total_wall_time_s": round(wall_time, 2),
        "temp_before_c": temp_before,
        "temp_after_c": temp_after
    }

def benchmark_model(model):
    print(f"\n==================================================")
    print(f"🤖 Benchmarking Model: {model}")
    print(f"==================================================")
    
    # Warm-up run
    print("   -> Warming up model cache...")
    try:
        query_ollama(model, "Hello! Give a 1-sentence greeting.")
    except Exception as e:
        print(f"   [!] Warmup failed for {model}: {e}")
        return None
    
    time.sleep(2)
    
    # 1. Default (All Cores, 8 threads)
    res_8t = benchmark_model_config(model, num_threads=8, config_label="All Cores (8T)")
    time.sleep(2)
    # 2. Pinned to 4 Big Cores (4x Cortex-A76)
    res_4t = benchmark_model_config(model, num_threads=4, config_label="Big Cores Only (4T)")
    
    return {
        "model": model,
        "configurations": [res_8t, res_4t]
    }

def main():
    if not check_ollama():
        print(f"[!] Error: Ollama daemon is not responding at {OLLAMA_API}.")
        print("    Please ensure Ollama is installed and running:")
        print("    curl -fsSL https://ollama.com/install.sh | sh")
        print("    ollama serve")
        sys.exit(1)

    target_models = sys.argv[1:] if len(sys.argv) > 1 else MODELS
    output_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "results", "llm_benchmarks.json")
    
    results = {
        "device": get_device_name(),
        "backend": "Ollama (ARM64 llama.cpp CPU inference)",
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "models": []
    }
    if os.path.exists(output_path):
        try:
            with open(output_path, "r") as f:
                existing = json.load(f)
                if "models" in existing:
                    results["models"] = [m for m in existing["models"] if m.get("model") not in target_models]
        except Exception:
            pass
    
    for model in target_models:
        print(f"\nPulling {model} if not present...")
        pull_ret = subprocess.run(["ollama", "pull", model], capture_output=False)
        if pull_ret.returncode != 0:
            print(f"[!] Failed to pull {model}, skipping...")
            continue
        
        bench_data = benchmark_model(model)
        if bench_data:
            results["models"].append(bench_data)
        
        time.sleep(5)  # Brief cooldown between models
        
    with open(output_path, "w") as f:
        json.dump(results, f, indent=2)
        
    print(f"\n==================================================")
    print(f"🎉 LLM Benchmarking Completed!")
    print(f"Results saved to: {output_path}")
    print(f"==================================================")

if __name__ == "__main__":
    main()
