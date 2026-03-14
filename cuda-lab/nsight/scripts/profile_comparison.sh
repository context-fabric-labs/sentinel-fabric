#!/bin/bash
# Nsight Systems Comparative Profiling Script
# 
# This script runs all kernels and generates a comparative analysis.
# It's useful for comparing performance across kernel variants.
#
# Usage:
#   ./profile_comparison.sh [output_dir]

set -e

OUTPUT_DIR="${1:-./nsight/reports}"
BENCHMARK="./bench_page_copy"

echo "========================================"
echo "  Nsight Systems: Comparative Analysis"
echo "========================================"
echo ""

# Run benchmark with all kernels and collect timing
echo "Running comparative benchmark..."
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Run benchmark with matrix mode to get all kernels
"$BENCHMARK" \
    --page-size 65536 \
    --num-pages 100 \
    --iterations 10 \
    --kernel all \
    --json "$OUTPUT_DIR/comparison_summary.json"

echo ""
echo "========================================"
echo "  Comparative Analysis Complete"
echo "========================================"
echo ""
echo "Results saved to:"
echo "  - $OUTPUT_DIR/comparison_summary.json"
echo ""
echo "To view JSON results:"
echo "  cat $OUTPUT_DIR/comparison_summary.json | jq"
echo ""
echo "For detailed timeline analysis, run:"
echo "  ./profile_page_copy.sh $OUTPUT_DIR"
echo ""
