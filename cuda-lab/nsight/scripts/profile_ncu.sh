#!/bin/bash
# Nsight Compute Profiling Script for Page Copy Kernels
# 
# This script runs Nsight Compute (ncu) profiling on all page copy kernel variants.
# It collects key hardware counters for performance analysis.
#
# Prerequisites:
# - Nsight Compute installed (ncu command)
# - CUDA-capable GPU
# - Built benchmark executable
# - May require sudo for some metrics
#
# Usage:
#   ./profile_ncu.sh [output_dir] [kernel_name]
#
# Examples:
#   ./profile_ncu.sh ./nsight/reports              # Profile all kernels
#   ./profile_ncu.sh ./nsight/reports coalesced    # Profile specific kernel

set -e

# Configuration
OUTPUT_DIR="${1:-./nsight/reports}"
TARGET_KERNEL="${2:-all}"
BENCHMARK="./bench_page_copy"

# Nsight Compute configuration
# Using "memory" and "occupancy" sections for page copy analysis
# These are most relevant for memory-bound kernels
NCU_SECTIONS="--section MemoryWorkloadAnalysis --section Occupancy --section CacheStats --section InstructionStats"
NCU_METRICS="--metrics l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum,l1tex__t_sectors_pipe_lsu_mem_global_op_st.sum,dram__throughput.avg.pct_of_peak_sustained_active,sm__warps_per_sm.avg"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "========================================"
echo "  Nsight Compute Profiling"
echo "  Page Copy Kernels"
echo "========================================"
echo ""

# Check if ncu is available
if ! command -v ncu &> /dev/null; then
    echo -e "${RED}Error: ncu (Nsight Compute) not found${NC}"
    echo "Please install Nsight Compute from:"
    echo "https://developer.nvidia.com/nsight-compute"
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
echo "  Target kernel: $TARGET_KERNEL"
echo "  Nsight Compute sections: $NCU_SECTIONS"
echo ""

# Function to profile a single kernel variant
profile_kernel_ncu() {
    local kernel_name=$1
    local output_name=$2
    
    echo -e "${YELLOW}Profiling with Nsight Compute: $kernel_name${NC}"
    echo -e "${BLUE}Collecting: Memory, Occupancy, Cache, Instruction metrics${NC}"
    echo ""
    
    # Run ncu profile
    # Using --launch-skip 0 --launch-count 1 to profile first launch
    # Using --target-processes all to profile child processes
    ncu profile \
        $NCU_SECTIONS \
        $NCU_METRICS \
        --launch-skip 0 \
        --launch-count 1 \
        --target-processes all \
        --import-source yes \
        --output "$OUTPUT_DIR/$output_name" \
        "$BENCHMARK" \
        --page-size 65536 \
        --num-pages 100 \
        --iterations 1 \
        --kernel "$kernel_name" \
        2>&1 | tee "$OUTPUT_DIR/${output_name}_log.txt"
    
    echo ""
    echo -e "${GREEN}✓ Complete: $output_name${NC}"
    echo "  Report: $OUTPUT_DIR/$output_name.ncu-rep"
    echo "  Log: $OUTPUT_DIR/${output_name}_log.txt"
    echo ""
}

# Determine which kernels to profile
if [ "$TARGET_KERNEL" = "all" ]; then
    KERNELS=("naive" "coalesced_scalar" "coalesced" "shared_mem" "shared_mem_vec")
else
    KERNELS=("$TARGET_KERNEL")
fi

echo "========================================"
echo "  Profiling Kernels with Nsight Compute"
echo "========================================"
echo ""

# Profile each kernel
for kernel in "${KERNELS[@]}"; do
    profile_kernel_ncu "$kernel" "ncu_page_copy_${kernel}"
done

echo "========================================"
echo "  Nsight Compute Profiling Complete"
echo "========================================"
echo ""
echo "Generated files:"
echo "  - $OUTPUT_DIR/ncu_page_copy_*.ncu-rep  (Nsight Compute reports)"
echo "  - $OUTPUT_DIR/ncu_page_copy_*_log.txt  (Console output)"
echo ""
echo "To view reports:"
echo "  ncu-ui $OUTPUT_DIR/ncu_page_copy_coalesced.ncu-rep"
echo ""
echo "Or from command line:"
echo "  ncu --view $OUTPUT_DIR/ncu_page_copy_coalesced.ncu-rep"
echo ""
echo "To compare kernels:"
echo "  ./nsight/scripts/compare_kernels.sh $OUTPUT_DIR"
echo ""

# Generate quick comparison from logs
echo "========================================"
echo "  Quick Performance Comparison"
echo "========================================"
echo ""

echo "Extracting kernel execution times from logs..."
echo ""
printf "%-30s %15s %20s\n" "Kernel" "Duration (ms)" "Memory Throughput"
printf "%-30s %15s %20s\n" "------------------------------" "---------------" "--------------------"

for kernel in "${KERNELS[@]}"; do
    log_file="$OUTPUT_DIR/ncu_page_copy_${kernel}_log.txt"
    if [ -f "$log_file" ]; then
        # Extract duration from log (simplified - actual parsing depends on ncu output format)
        duration=$(grep -i "Duration" "$log_file" | head -1 | awk '{print $2}' || echo "N/A")
        throughput=$(grep -i "dram__throughput" "$log_file" | head -1 | awk '{print $2}' || echo "N/A")
        printf "%-30s %15s %20s\n" "$kernel" "$duration" "$throughput"
    fi
done

echo ""
echo "========================================"
echo "  Next Steps"
echo "========================================"
echo ""
echo "1. Open reports in Nsight Compute UI:"
echo "   ncu-ui $OUTPUT_DIR/ncu_page_copy_coalesced.ncu-rep"
echo ""
echo "2. Compare key metrics:"
echo "   - Memory throughput (dram__throughput)"
echo "   - L2 cache hit rate"
echo "   - Occupancy"
echo "   - Register usage"
echo ""
echo "3. Run comparison script:"
echo "   ./nsight/scripts/compare_kernels.sh $OUTPUT_DIR"
echo ""
echo "4. Read analysis guide:"
echo "   See docs/NSIGHT_COMPUTE_GUIDE.md"
echo ""
