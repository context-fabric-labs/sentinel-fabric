#!/bin/bash
# Run transfer benchmarks

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"
BENCHMARK="$BUILD_DIR/bench_transfer"

echo "=== PM-NIXL Transfer Benchmark Runner ==="
echo ""

# Check if benchmark exists
if [ ! -f "$BENCHMARK" ]; then
    echo "Error: Benchmark executable not found at $BENCHMARK"
    echo "Please build with: cmake .. -DBUILD_BENCHMARKS=ON && make"
    exit 1
fi

# Default parameters
SIZES="1024,4096,16384,65536,262144,1048576,4194304,16777216,67108864"
WARMUP=10
ITERATIONS=100
DEVICE=0
OUTPUT_DIR="$SCRIPT_DIR/../build/benchmark_results"

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --sizes)
            SIZES="$2"
            shift 2
            ;;
        --warmup)
            WARMUP="$2"
            shift 2
            ;;
        --iterations)
            ITERATIONS="$2"
            shift 2
            ;;
        --device)
            DEVICE="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --sizes SIZES      Comma-separated sizes (default: $SIZES)"
            echo "  --warmup N         Warmup iterations (default: $WARMUP)"
            echo "  --iterations N     Benchmark iterations (default: $ITERATIONS)"
            echo "  --device N         CUDA device (default: $DEVICE)"
            echo "  --help             Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Timestamp for output files
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CSV_FILE="$OUTPUT_DIR/benchmark_${TIMESTAMP}.csv"
JSON_FILE="$OUTPUT_DIR/benchmark_${TIMESTAMP}.json"

echo "Running benchmark..."
echo ""

# Run benchmark with CSV and JSON output
"$BENCHMARK" \
    --sizes "$SIZES" \
    --warmup "$WARMUP" \
    --iterations "$ITERATIONS" \
    --device "$DEVICE" \
    --csv "$CSV_FILE" \
    --json "$JSON_FILE"

echo ""
echo "Benchmark complete!"
echo ""
echo "Output files:"
echo "  CSV:  $CSV_FILE"
echo "  JSON: $JSON_FILE"
echo ""
echo "To view results:"
echo "  cat $CSV_FILE"
echo "  cat $JSON_FILE | jq"
echo ""
