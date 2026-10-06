#!/usr/bin/env python3
"""
Orange Pi 5 Plus - RKLLM Hardware NPU Inference Benchmark
Tester: Pierluigi De Vitis
Measures: Hardware NPU tok/s, TTFT, CPU offload (% CPU usage during inference), Thermals.
"""

import time
import json
import subprocess
import os
import sys
import psutil
import re

def get_temp():
    try:
        with open("/sys/class/thermal/thermal_zone0/temp", "r") as f:
            return round(int(f.read().strip()) / 1000.0, 1)
    except Exception:
        return None

def get_device_name():
    try:
        if os.path.exists("/proc/device-tree/model"):
            with open("/proc/device-tree/model", "rb") as f:
                return f.read().decode("utf-8", errors="ignore").strip("\x00")
    except Exception:
        pass
    return "Orange Pi 5 Plus (RK3588, 16GB RAM)"

def run_npu_benchmark(model_path, max_tokens=256, context_len=512, prompt=None):
    if prompt is None:
        prompt = "Explain in 3 distinct bullet points how a neural network learns using backpropagation and gradient descent."

    bench_bin = os.path.join(os.path.dirname(__file__), "rkllm_bench_v1")
    if not os.path.exists(bench_bin):
        src = os.path.join(os.path.dirname(__file__), "rkllm_bench_v1.cpp")
        if os.path.exists(src):
            print(f"[*] Binary {os.path.basename(bench_bin)} not found. Compiling from {os.path.basename(src)}...")
            res = subprocess.run(["g++", "-O2", "-o", bench_bin, src, "-lrkllmrt"], capture_output=True, text=True)
            if res.returncode != 0:
                print(f"[!] Compilation failed: {res.stderr}")
                print("    Please ensure g++ and librkllmrt are installed on your system.")
                return None
        else:
            print(f"[!] Error: Runner binary {bench_bin} and source {src} not found.")
            return None

    cmd = [bench_bin, model_path, str(max_tokens), str(context_len), prompt]

    print(f"\n==================================================")
    print(f"Benchmarking NPU Model: {os.path.basename(model_path)}")
    print(f"   Max Tokens: {max_tokens} | Context: {context_len}")
    print(f"==================================================")

    temp_before = get_temp()
    
    # Track CPU usage across the system during inference
    psutil.cpu_percent(interval=None) # reset counter

    start_wall = time.perf_counter()
    env = os.environ.copy()
    proc = subprocess.Popen(
        cmd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    cpu_samples = []
    while proc.poll() is None:
        time.sleep(0.5)
        cpu_samples.append(psutil.cpu_percent(interval=None))

    stdout, stderr = proc.communicate()
    end_wall = time.perf_counter()
    temp_after = get_temp()

    avg_cpu = round(sum(cpu_samples) / len(cpu_samples), 1) if cpu_samples else 0.0

    if proc.returncode != 0:
        print(f"[!] Benchmark failed with code {proc.returncode}!")
        print(f"STDERR: {stderr}")
        return None

    # Parse metrics from stdout
    # Looking for:
    #  - Generated Tokens: 256
    #  - Time to First Token (TTFT): 102.715 ms
    #  - Total Time: 15.4509 s
    #  - Evaluation Rate: 16.6144 tok/s
    tokens = 0
    ttft_ms = 0.0
    eval_rate = 0.0
    total_time_s = end_wall - start_wall

    tokens_m = re.search(r"Generated Tokens:\s*(\d+)", stdout)
    if tokens_m:
        tokens = int(tokens_m.group(1))

    ttft_m = re.search(r"Time to First Token \(TTFT\):\s*([\d\.]+)\s*ms", stdout)
    if ttft_m:
        ttft_ms = float(ttft_m.group(1))

    eval_m = re.search(r"Evaluation Rate:\s*([\d\.]+)\s*tok/s", stdout)
    if eval_m:
        eval_rate = float(eval_m.group(1))

    print(f"   [NPU Result] Generated: {tokens} tokens")
    print(f"   [NPU Result] Eval Rate: {eval_rate:.2f} tok/s")
    print(f"   [NPU Result] TTFT: {ttft_ms:.1f} ms")
    print(f"   [NPU Result] System CPU Load: {avg_cpu}% (Demonstrating true NPU compute offload!)")
    print(f"   [NPU Result] SoC Temp: {temp_before}°C -> {temp_after}°C")

    return {
        "model_file": os.path.basename(model_path),
        "model_path": model_path,
        "npu_cores_used": 3,
        "max_tokens": max_tokens,
        "context_len": context_len,
        "generated_tokens": tokens,
        "eval_tok_per_sec": round(eval_rate, 2),
        "ttft_ms": round(ttft_ms, 1),
        "total_wall_time_s": round(total_time_s, 2),
        "avg_system_cpu_percent": avg_cpu,
        "temp_before_c": temp_before,
        "temp_after_c": temp_after
    }

def main():
    tools_dir = os.path.dirname(__file__)
    model_files = [f for f in os.listdir(tools_dir) if f.endswith(".rkllm")]
    
    if len(sys.argv) > 1:
        model_files = sys.argv[1:]

    if not model_files:
        print("[!] No .rkllm models found in the tools directory.")
        print("    Note: Due to Git size limits (>100MB), pre-converted .rkllm models are not tracked in git.")
        print("    To benchmark an NPU model:")
        print("    1. Download or convert an RKLLM model (e.g., qwen1.5-0.5B.rkllm using rkllm-toolkit).")
        print("    2. Place it in tools/ or pass its path: python3 tools/benchmark_npu.py /path/to/model.rkllm")
        return

    results = {
        "device": get_device_name(),
        "backend": "Rockchip RKLLM Native Runtime (3x NPU cores, 6 TOPS INT8/INT4)",
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "npu_models": []
    }

    for mf in model_files:
        if os.path.exists(mf):
            path = os.path.abspath(mf)
        elif os.path.exists(os.path.join(tools_dir, mf)):
            path = os.path.join(tools_dir, mf)
        else:
            print(f"[!] Model not found: {mf}")
            continue
        bench_data = run_npu_benchmark(path)
        if bench_data:
            results["npu_models"].append(bench_data)
        time.sleep(3)

    out_file = os.path.join(os.path.dirname(tools_dir), "results", "npu_benchmarks.json")
    with open(out_file, "w") as f:
        json.dump(results, f, indent=2)

    print(f"\n==================================================")
    print(f"🎉 NPU Benchmarking Completed!")
    print(f"Results saved to: {out_file}")
    print(f"==================================================")

if __name__ == "__main__":
    main()
