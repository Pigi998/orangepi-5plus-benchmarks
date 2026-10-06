# Makefile for RKLLM NPU Benchmark Tool
CXX ?= g++
CXXFLAGS ?= -O2 -Wall -std=c++17
LDFLAGS ?= -lrkllmrt

TARGET = rkllm_bench_v1
SRC = rkllm_bench_v1.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET) rkllm_bench rkllm_bench_114
