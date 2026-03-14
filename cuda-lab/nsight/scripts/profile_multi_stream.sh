#!/bin/bash
# Nsight Systems Multi-Stream Profiling Script
# 
# This script profiles multi-stream concurrent execution to observe overlap.
# It demonstrates how to use Nsight Systems to analyze concurrent kernel execution.
#
# Usage:
#   ./profile_multi_stream.sh [output_dir]

set -e

OUTPUT_DIR="${1:-./nsight/reports}"
BENCHMARK="./bench_page_copy"

echo "========================================"
echo "  Nsight Systems: Multi-Stream Profiling"
echo "========================================"
echo ""

# Create a simple multi-stream test program
cat > /tmp/multi_stream_test.cu << 'EOF'
#include <cuda_runtime.h>
#include <stdio.h>

__global__ void page_copy_kernel(void* dst, const void* src, size_t size) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size) {
        ((char*)dst)[idx] = ((const char*)src)[idx];
    }
}

int main(int argc, char** argv) {
    const size_t size = 64 * 1024 * 1024;  // 64MB
    const int num_streams = 4;
    
    void *d_src, *d_dst;
    cudaMalloc(&d_src, size);
    cudaMalloc(&d_dst, size);
    
    // Create streams
    cudaStream_t streams[num_streams];
    for (int i = 0; i < num_streams; ++i) {
        cudaStreamCreate(&streams[i]);
    }
    
    // Launch kernels in different streams
    int threads_per_block = 256;
    int num_blocks = (size + threads_per_block - 1) / threads_per_block;
    
    for (int i = 0; i < num_streams; ++i) {
        page_copy_kernel<<<num_blocks, threads_per_block, 0, streams[i]>>>(
            d_dst, d_src, size / num_streams
        );
    }
    
    // Synchronize all streams
    for (int i = 0; i < num_streams; ++i) {
        cudaStreamSynchronize(streams[i]);
        cudaStreamDestroy(streams[i]);
    }
    
    cudaFree(d_src);
    cudaFree(d_dst);
    
    printf("Multi-stream test complete\n");
    return 0;
}
EOF

# Compile the test program
echo "Compiling multi-stream test program..."
nvcc -o /tmp/multi_stream_test /tmp/multi_stream_test.cu

# Profile with Nsight Systems
echo ""
echo "Profiling multi-stream execution..."
echo ""

nsys profile \
    --stats=true \
    --backtrace=none \
    --trace=cuda,nvtx \
    --output="$OUTPUT_DIR/multi_stream" \
    --force-overwrite=true \
    /tmp/multi_stream_test

echo ""
echo "========================================"
echo "  Multi-Stream Profiling Complete"
echo "========================================"
echo ""
echo "Generated files:"
echo "  - $OUTPUT_DIR/multi_stream.nsys-rep"
echo ""
echo "To view in Nsight Systems GUI:"
echo "  nsys-ui $OUTPUT_DIR/multi_stream.nsys-rep"
echo ""
echo "What to look for:"
echo "  1. Multiple concurrent kernel executions"
echo "  2. GPU utilization across streams"
echo "  3. Memory copy overlap"
echo "  4. Stream ordering"
echo ""

# Cleanup
rm -f /tmp/multi_stream_test /tmp/multi_stream_test.cu
