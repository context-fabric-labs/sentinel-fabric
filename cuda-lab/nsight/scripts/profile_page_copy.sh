#!/bin/bash
# Nsight Systems Profiling Script for Page Copy Kernels
# 
# This script runs Nsight Systems profiling on all page copy kernel variants.
# It generates timeline reports that can be analyzed in Nsight Systems GUI.
#
# Prerequisites:
# - Nsight Systems installed (nsys command)
# - CUDA-capable GPU
# - Built benchmark executable
#
# Usage:
#   ./profile_page_copy.sh [output_dir]
#
# Example:
#   ./profile_page_copy.sh ./nsight/reports

set -e

# Configuration
OUTPUT_DIR="${1:-./nsight/reports}"
BENCHMARK="./bench_page_copy"
PAGE_SIZE=${PAGE_SIZE:-65536}  # 64KB default
NUM_PAGES=${NUM_PAGES:-100}
ITERATIONS=${ITERATIONS:-10}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "  Nsight Systems Profiling"
echo "  Page Copy Kernels"
echo "========================================"
echo ""

# Check if nsys is available
if ! command -v nsys &> /dev/null; then
    echo -e "${RED}Error: nsys (Nsight Systems) not found${NC}"
    echo "Please install Nsight Systems from:"
    echo "https://developer.nvidia.com/nsight-systems"
    exit 1
fi

# Check if benchmark exists
if [ ! -f "$BENCHMARK" ]; then
    echo -e "${RED}Error: Benchmark executable not found: $BENCHMARK${NC}"
    echo "Please build the project first:"
    echo "  cd build && make -j\$(nproc)"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo "Configuration:"
echo "  Output directory: $OUTPUT_DIR"
echo "  Page size: $PAGE_SIZE bytes"
echo "  Num pages: $NUM_PAGES"
echo "  Iterations: $ITERATIONS"
echo ""

# Function to profile a single kernel variant
profile_kernel() {
    local kernel_name=$1
    local output_name=$2
    
    echo -e "${YELLOW}Profiling: $kernel_name${NC}"
    
    # Run nsys profile
    nsys profile \
        --stats=true \
        --backtrace=none \
        --trace=cuda,nvtx \
        --output="$OUTPUT_DIR/$output_name" \
        --force-overwrite=true \
        "$BENCHMARK" \
        --page-size "$PAGE_SIZE" \
        --num-pages "$NUM_PAGES" \
        --iterations "$ITERATIONS" \
        --kernel "$kernel_name" \
        --json "$OUTPUT_DIR/${output_name}_summary.json"
    
    echo -e "${GREEN}✓ Complete: $output_name${NC}"
    echo ""
}

# Profile all kernel variants
echo "========================================"
echo "  Profiling All Kernel Variants"
echo "========================================"
echo ""

# Naive baseline
profile_kernel "naive" "page_copy_naive"

# Coalesced scalar
profile_kernel "coalesced_scalar" "page_copy_coalesced_scalar"

# Coalesced vectorized (best)
profile_kernel "coalesced" "page_copy_coalesced"

# Shared memory (educational)
profile_kernel "shared_mem" "page_copy_shared_mem"

# Shared memory vectorized
profile_kernel "shared_mem_vec" "page_copy_shared_mem_vec"

echo "========================================"
echo "  Profiling Complete"
echo "========================================"
echo ""
echo "Generated files:"
echo "  - $OUTPUT_DIR/page_copy_*.nsys-rep  (Nsight Systems reports)"
echo "  - $OUTPUT_DIR/page_copy_*_summary.json  (Summary JSON)"
echo ""
echo "To view reports:"
echo "  nsys-ui $OUTPUT_DIR/page_copy_coalesced.nsys-rep"
echo ""
echo "Or from command line:"
echo "  nsys export --type text $OUTPUT_DIR/page_copy_coalesced.nsys-rep"
echo ""

# Generate comparison summary
echo "========================================"
echo "  Generating Comparison Summary"
echo "========================================"
echo ""

# Extract timing from JSON summaries
echo "Kernel Performance Summary:"
echo ""
printf "%-30s %15s %15s\n" "Kernel" "Avg Latency (ms)" "Throughput (GB/s)"
printf "%-30s %15s %15s\n" "------------------------------" "---------------" "---------------"

for kernel in naive coalesced_scalar coalesced shared_mem shared_mem_vec; do
    json_file="$OUTPUT_DIR/page_copy_${kernel}_summary.json"
    if [ -f "$json_file" ]; then
        # Extract latency and throughput from JSON (simplified)
        # In practice, you'd parse the JSON properly
        echo "  $kernel: See $json_file for details"
    fi
done

echo ""
echo "========================================"
echo "  Next Steps"
echo "========================================"
echo ""
echo "1. Open reports in Nsight Systems GUI:"
echo "   nsys-ui $OUTPUT_DIR/page_copy_coalesced.nsys-rep"
echo ""
echo "2. Compare kernel timelines:"
echo "   - Look at kernel durations"
echo "   - Check memory copy operations"
echo "   - Observe GPU utilization"
echo ""
echo "3. Read timeline analysis guide:"
echo "   See docs/NSIGHT_SYSTEMS_GUIDE.md"
echo ""
