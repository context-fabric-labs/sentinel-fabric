#!/bin/bash
# Nsight Compute Kernel Comparison Script
# 
# This script compares Nsight Compute reports across different kernel variants.
# It extracts key metrics and generates a comparison table.
#
# Usage:
#   ./compare_kernels.sh [reports_dir]

set -e

REPORTS_DIR="${1:-./nsight/reports}"

echo "========================================"
echo "  Nsight Compute: Kernel Comparison"
echo "========================================"
echo ""

# Check if reports directory exists
if [ ! -d "$REPORTS_DIR" ]; then
    echo "Error: Reports directory not found: $REPORTS_DIR"
    exit 1
fi

# Check if ncu is available
if ! command -v ncu &> /dev/null; then
    echo "Error: ncu (Nsight Compute) not found"
    exit 1
fi

echo "Analyzing reports in: $REPORTS_DIR"
echo ""

# Create comparison CSV
COMPARISON_CSV="$REPORTS_DIR/kernel_comparison.csv"

echo "Kernel,Duration_ms,Memory_Throughput_GB_s,L2_Hit_Rate_pct,Occupancy_pct,Registers_per_thread,Shared_Memory_KB" > "$COMPARISON_CSV"

# Extract metrics from each report
for rep_file in "$REPORTS_DIR"/ncu_page_copy_*.ncu-rep; do
    if [ -f "$rep_file" ]; then
        # Extract kernel name from filename
        filename=$(basename "$rep_file")
        kernel_name=$(echo "$filename" | sed 's/ncu_page_copy_//' | sed 's/.ncu-rep//')
        
        echo "Processing: $kernel_name"
        
        # Extract metrics using ncu command
        # Note: Actual metric extraction depends on ncu output format
        # This is a simplified example - in practice, you'd parse the actual ncu output
        
        # Try to extract from log file if available
        log_file="${rep_file%.ncu-rep}_log.txt"
        
        if [ -f "$log_file" ]; then
            # Extract duration (simplified parsing)
            duration=$(grep -i "Duration" "$log_file" 2>/dev/null | head -1 | grep -oE '[0-9]+\.?[0-9]*' | head -1 || echo "N/A")
            
            # Extract memory throughput
            throughput=$(grep -i "dram__throughput" "$log_file" 2>/dev/null | head -1 | grep -oE '[0-9]+\.?[0-9]*' | head -1 || echo "N/A")
            
            # Extract L2 hit rate
            l2_hit=$(grep -i "l2.*hit" "$log_file" 2>/dev/null | head -1 | grep -oE '[0-9]+\.?[0-9]*' | head -1 || echo "N/A")
            
            # Extract occupancy
            occupancy=$(grep -i "occupancy" "$log_file" 2>/dev/null | head -1 | grep -oE '[0-9]+\.?[0-9]*' | head -1 || echo "N/A")
            
            # Extract registers
            registers=$(grep -i "register" "$log_file" 2>/dev/null | head -1 | grep -oE '[0-9]+' | head -1 || echo "N/A")
            
            # Extract shared memory
            shared_mem=$(grep -i "shared" "$log_file" 2>/dev/null | head -1 | grep -oE '[0-9]+\.?[0-9]*' | head -1 || echo "N/A")
            
            # Write to CSV
            echo "$kernel_name,$duration,$throughput,$l2_hit,$occupancy,$registers,$shared_mem" >> "$COMPARISON_CSV"
            
            echo "  Duration: ${duration:-N/A} ms"
            echo "  Throughput: ${throughput:-N/A} GB/s"
            echo ""
        else
            echo "  Log file not found: $log_file"
            echo "  Skipping..."
            echo ""
        fi
    fi
done

echo "========================================"
echo "  Comparison Summary"
echo "========================================"
echo ""
echo "Comparison saved to: $COMPARISON_CSV"
echo ""

# Display comparison table
if [ -f "$COMPARISON_CSV" ]; then
    echo "Kernel Performance Comparison:"
    echo ""
    cat "$COMPARISON_CSV" | column -t -s','
    echo ""
fi

echo "========================================"
echo "  Interpretation Guide"
echo "========================================"
echo ""
echo "Key Metrics to Compare:"
echo ""
echo "1. Memory Throughput (dram__throughput)"
echo "   - Higher is better"
echo "   - Coalesced should show 10-50x improvement over naive"
echo "   - Indicates better memory utilization"
echo ""
echo "2. L2 Cache Hit Rate"
echo "   - Higher is better"
echo "   - Coalesced access should improve L2 hit rate"
echo "   - Indicates better cache utilization"
echo ""
echo "3. Occupancy"
echo "   - Percentage of active warps"
echo "   - Higher occupancy can hide latency"
echo "   - But doesn't always mean faster!"
echo ""
echo "4. Registers per Thread"
echo "   - Lower is generally better"
echo "   - High register usage limits occupancy"
echo "   - Can cause register spilling"
echo ""
echo "5. Shared Memory"
echo "   - Bytes per block"
echo "   - Shared mem kernels should show usage"
echo "   - But may not help for simple copy"
echo ""
echo "========================================"
echo "  Expected Findings"
echo "========================================"
echo ""
echo "Naive Kernel:"
echo "  - Low memory throughput"
echo "  - Low L2 hit rate"
echo "  - Low occupancy (1 thread = 1 byte)"
echo ""
echo "Coalesced Kernel:"
echo "  - High memory throughput (10-50x naive)"
echo "  - Better L2 hit rate"
echo "  - Better occupancy (16 bytes per thread)"
echo ""
echo "Shared Memory Kernel:"
echo "  - Medium memory throughput"
echo "  - Shared memory usage visible"
echo "  - Extra overhead from shared mem copies"
echo ""
echo "========================================"
echo "  Next Steps"
echo "========================================"
echo ""
echo "1. Open detailed reports:"
echo "   ncu-ui $REPORTS_DIR/ncu_page_copy_coalesced.ncu-rep"
echo ""
echo "2. Compare specific metrics:"
echo "   ncu --view $REPORTS_DIR/ncu_page_copy_naive.ncu-rep"
echo "   ncu --view $REPORTS_DIR/ncu_page_copy_coalesced.ncu-rep"
echo ""
echo "3. Read detailed analysis guide:"
echo "   See docs/NSIGHT_COMPUTE_GUIDE.md"
echo ""
