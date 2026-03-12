#!/bin/bash
# Run All Experiments

set -e

echo "=========================================="
echo "Sentinel Fabric - Experiment Suite"
echo "=========================================="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXPERIMENTS_DIR="$(dirname "$SCRIPT_DIR")"
RESULTS_DIR="$EXPERIMENTS_DIR/results"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to run experiment
run_experiment() {
    local exp_num=$1
    local exp_name=$2
    
    echo ""
    echo "=========================================="
    echo "Running Experiment $exp_num: $exp_name"
    echo "=========================================="
    
    cd "$EXPERIMENTS_DIR/scenarios/$exp_num"*
    
    if [ -f "run.sh" ]; then
        bash run.sh
    else
        echo "No run.sh found for experiment $exp_num"
        exit 1
    fi
    
    echo "Experiment $exp_num complete!"
}

# Run all experiments
run_experiment "01" "Random vs Sticky Routing"
run_experiment "02" "KV Pressure Mix"
run_experiment "03" "GPU Headroom Stress"
run_experiment "04" "Overload with Bounded Inflight"
run_experiment "05" "Unhealthy Pod Behavior"

echo ""
echo "=========================================="
echo "All Experiments Complete!"
echo "=========================================="
echo ""
echo "Results saved to: $RESULTS_DIR"
echo ""
echo "To view results:"
echo "  cat $RESULTS_DIR/*/results.json | jq"
echo ""
echo "To generate graphs:"
echo "  python3 $SCRIPT_DIR/generate_graphs.py"
echo ""
