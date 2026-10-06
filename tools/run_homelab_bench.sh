#!/usr/bin/env bash
# ==============================================================================
# Orange Pi 5 Plus - Unified Homelab & System Benchmark Suite
# Tester: Pierluigi De Vitis (Independent Community Benchmark)
# Hardware: Rockchip RK3588 (8-Core), 16GB RAM, Samsung PM981a 256GB NVMe SSD
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_DIR="$(dirname "$SCRIPT_DIR")"
RESULTS_DIR="${BASE_DIR}/results"
TEMP_DIR="${BASE_DIR}/.bench_scratch"

mkdir -p "$RESULTS_DIR" "$TEMP_DIR"

LOG_FILE="${RESULTS_DIR}/benchmark_run.log"
exec > >(tee -a "$LOG_FILE") 2>&1

# Verify required dependencies
REQUIRED_PKGS=("fio" "sysbench" "7z" "stress-ng" "jq")
MISSING_PKGS=()
for pkg in "${REQUIRED_PKGS[@]}"; do
    if ! command -v "$pkg" >/dev/null 2>&1; then
        MISSING_PKGS+=("$pkg")
    fi
done

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    echo "[!] Error: Missing required benchmark dependencies: ${MISSING_PKGS[*]}"
    echo "    Please run: sudo apt update && sudo apt install -y fio sysbench p7zip-full stress-ng jq"
    exit 1
fi

DEVICE_NAME="Orange Pi 5 Plus"
if [ -f /proc/device-tree/model ]; then
    DEVICE_NAME="$(tr -d '\0' < /proc/device-tree/model)"
fi

echo "======================================================================"
echo "STARTING HARDWARE BENCHMARK RUN: ${DEVICE_NAME}"
echo "Timestamp: $(date '+%Y-%m-%d %H:%M:%S %Z')"
echo "Host: $(uname -n) | Kernel: $(uname -r) | Arch: $(uname -m)"
echo "======================================================================"

get_temp() {
    if [ -f /sys/class/thermal/thermal_zone0/temp ]; then
        awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp
    else
        echo "N/A"
    fi
}

echo ""
echo "Baseline Idle Temperature: $(get_temp)°C"

# ------------------------------------------------------------------------------
# 1. CPU & RAM BENCHMARK: 7-Zip Compression / Decompression
# ------------------------------------------------------------------------------
echo ""
echo "----------------------------------------------------------------------"
echo "[1/4] Running 7-Zip CPU Benchmark (p7zip MIPS)..."
echo "----------------------------------------------------------------------"

echo "-> Single-threaded 7-Zip Benchmark..."
7z b -mmt1 > "${RESULTS_DIR}/7zip_single.txt" 2>&1
SINGLE_MIPS=$(grep "Tot:" "${RESULTS_DIR}/7zip_single.txt" | tail -n 1 | awk '{print $4}')
echo "   Single-Core Rating: ${SINGLE_MIPS} MIPS"

echo "-> Multi-threaded (8 cores) 7-Zip Benchmark..."
7z b -mmt8 > "${RESULTS_DIR}/7zip_multi.txt" 2>&1
MULTI_MIPS=$(grep "Tot:" "${RESULTS_DIR}/7zip_multi.txt" | tail -n 1 | awk '{print $4}')
echo "   Multi-Core (8T) Rating: ${MULTI_MIPS} MIPS"
echo "   CPU Temp after 7-Zip: $(get_temp)°C"

# ------------------------------------------------------------------------------
# 2. CPU BENCHMARK: Sysbench CPU
# ------------------------------------------------------------------------------
echo ""
echo "----------------------------------------------------------------------"
echo "[2/4] Running Sysbench CPU Benchmark..."
echo "----------------------------------------------------------------------"
sysbench cpu --cpu-max-prime=20000 --threads=8 run > "${RESULTS_DIR}/sysbench_cpu.txt" 2>&1
SYSBENCH_EVENTS=$(grep "events per second:" "${RESULTS_DIR}/sysbench_cpu.txt" | awk '{print $4}')
echo "   Sysbench CPU (8T, prime=20000): ${SYSBENCH_EVENTS} events/sec"
echo "   CPU Temp after Sysbench: $(get_temp)°C"

# ------------------------------------------------------------------------------
# 3. STORAGE BENCHMARK: Samsung PM981a 256GB NVMe (FIO)
# ------------------------------------------------------------------------------
echo ""
echo "----------------------------------------------------------------------"
echo "[3/4] Running Storage Benchmarks (Samsung PM981a NVMe via FIO)..."
echo "----------------------------------------------------------------------"
TEST_FILE="${TEMP_DIR}/fio_test_file"

echo "-> Test 3.1: Sequential Read (1MB block, QD32, direct I/O, 4GB)..."
fio --name=seq_read \
    --filename="$TEST_FILE" \
    --rw=read \
    --bs=1M \
    --size=4G \
    --iodepth=32 \
    --direct=1 \
    --ioengine=libaio \
    --group_reporting \
    --output-format=json \
    --output="${RESULTS_DIR}/fio_seq_read.json" > /dev/null 2>&1

SEQ_READ_MB=$(jq -r '.jobs[0].read.bw_bytes / 1024 / 1024' "${RESULTS_DIR}/fio_seq_read.json" | LC_ALL=C xargs printf "%.2f")
echo "   Sequential Read: ${SEQ_READ_MB} MB/s"

echo "-> Test 3.2: Sequential Write (1MB block, QD32, direct I/O, 4GB)..."
fio --name=seq_write \
    --filename="$TEST_FILE" \
    --rw=write \
    --bs=1M \
    --size=4G \
    --iodepth=32 \
    --direct=1 \
    --ioengine=libaio \
    --group_reporting \
    --output-format=json \
    --output="${RESULTS_DIR}/fio_seq_write.json" > /dev/null 2>&1

SEQ_WRITE_MB=$(jq -r '.jobs[0].write.bw_bytes / 1024 / 1024' "${RESULTS_DIR}/fio_seq_write.json" | LC_ALL=C xargs printf "%.2f")
echo "   Sequential Write: ${SEQ_WRITE_MB} MB/s"

echo "-> Test 3.3: Random 4K Read (QD32, direct I/O, 1GB, 30s runtime)..."
fio --name=rand_read_4k \
    --filename="$TEST_FILE" \
    --rw=randread \
    --bs=4k \
    --size=1G \
    --iodepth=32 \
    --time_based \
    --runtime=20 \
    --direct=1 \
    --ioengine=libaio \
    --group_reporting \
    --output-format=json \
    --output="${RESULTS_DIR}/fio_rand_read_4k.json" > /dev/null 2>&1

RAND_READ_IOPS=$(jq -r '.jobs[0].read.iops' "${RESULTS_DIR}/fio_rand_read_4k.json" | LC_ALL=C xargs printf "%.0f")
echo "   Random 4K Read IOPS (QD32): ${RAND_READ_IOPS} IOPS"

echo "-> Test 3.4: Random 4K Write (QD32, direct I/O, 1GB, 30s runtime)..."
fio --name=rand_write_4k \
    --filename="$TEST_FILE" \
    --rw=randwrite \
    --bs=4k \
    --size=1G \
    --iodepth=32 \
    --time_based \
    --runtime=20 \
    --direct=1 \
    --ioengine=libaio \
    --group_reporting \
    --output-format=json \
    --output="${RESULTS_DIR}/fio_rand_write_4k.json" > /dev/null 2>&1

RAND_WRITE_IOPS=$(jq -r '.jobs[0].write.iops' "${RESULTS_DIR}/fio_rand_write_4k.json" | LC_ALL=C xargs printf "%.0f")
echo "   Random 4K Write IOPS (QD32): ${RAND_WRITE_IOPS} IOPS"

echo "-> Test 3.5: Mixed 70/30 R/W 4K (QD16, direct I/O, 1GB, 20s runtime)..."
fio --name=rand_mixed_4k \
    --filename="$TEST_FILE" \
    --rw=randrw \
    --rwmixread=70 \
    --bs=4k \
    --size=1G \
    --iodepth=16 \
    --time_based \
    --runtime=20 \
    --direct=1 \
    --ioengine=libaio \
    --group_reporting \
    --output-format=json \
    --output="${RESULTS_DIR}/fio_mixed_4k.json" > /dev/null 2>&1

MIXED_READ_IOPS=$(jq -r '.jobs[0].read.iops' "${RESULTS_DIR}/fio_mixed_4k.json" | LC_ALL=C xargs printf "%.0f")
MIXED_WRITE_IOPS=$(jq -r '.jobs[0].write.iops' "${RESULTS_DIR}/fio_mixed_4k.json" | LC_ALL=C xargs printf "%.0f")
echo "   Mixed 70/30 4K IOPS: ${MIXED_READ_IOPS} Read IOPS / ${MIXED_WRITE_IOPS} Write IOPS"

# Pulizia file temporaneo fio
rm -f "$TEST_FILE"
echo "   NVMe Temp after I/O: $(get_temp)°C"

# ------------------------------------------------------------------------------
# 4. THERMAL & SUSTAINED LOAD TEST (stress-ng)
# ------------------------------------------------------------------------------
echo ""
echo "----------------------------------------------------------------------"
echo "[4/4] Running Thermal Sustained Load Test (stress-ng 60s)..."
echo "----------------------------------------------------------------------"
START_TEMP=$(get_temp)
echo "   Pre-stress Temp: ${START_TEMP}°C"

stress-ng --cpu 8 --cpu-method all --timeout 60s --metrics-brief > "${RESULTS_DIR}/stress_ng.txt" 2>&1
PEAK_TEMP=$(get_temp)
echo "   Peak Temp during stress: ${PEAK_TEMP}°C"

# Raffreddamento 15 secondi
sleep 15
COOLDOWN_TEMP=$(get_temp)
echo "   Cooldown Temp (after 15s): ${COOLDOWN_TEMP}°C"

# ------------------------------------------------------------------------------
# 5. CONSOLIDAMENTO METRICHE IN FORMATO JSON
# ------------------------------------------------------------------------------
cat <<EOF > "${RESULTS_DIR}/system_benchmark_summary.json"
{
  "timestamp": "$(date -u +'%Y-%m-%dT%H:%M:%SZ')",
  "hardware": {
    "device": "${DEVICE_NAME}",
    "soc": "Rockchip RK3588",
    "ram": "16GB LPDDR4x",
    "storage": "Samsung PM981a 256GB NVMe PCIe 3.0 x4"
  },
  "cpu_benchmarks": {
    "7zip_single_core_mips": ${SINGLE_MIPS:-0},
    "7zip_multi_core_mips": ${MULTI_MIPS:-0},
    "sysbench_cpu_events_per_sec": ${SYSBENCH_EVENTS:-0}
  },
  "storage_fio": {
    "sequential_read_mb_s": ${SEQ_READ_MB:-0},
    "sequential_write_mb_s": ${SEQ_WRITE_MB:-0},
    "random_4k_read_iops": ${RAND_READ_IOPS:-0},
    "random_4k_write_iops": ${RAND_WRITE_IOPS:-0},
    "mixed_4k_read_iops": ${MIXED_READ_IOPS:-0},
    "mixed_4k_write_iops": ${MIXED_WRITE_IOPS:-0}
  },
  "thermals": {
    "idle_temp_c": ${START_TEMP:-0},
    "peak_temp_c": ${PEAK_TEMP:-0},
    "cooldown_temp_c": ${COOLDOWN_TEMP:-0}
  }
}
EOF

echo ""
echo "======================================================================"
echo "BENCHMARK RUN SUCCESSFULLY COMPLETED!"
echo "Summary saved to: ${RESULTS_DIR}/system_benchmark_summary.json"
echo "======================================================================"
