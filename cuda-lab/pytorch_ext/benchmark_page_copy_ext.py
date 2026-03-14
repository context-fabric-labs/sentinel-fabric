"""
PyTorch Extension Benchmark

Compares the custom page copy extension against PyTorch's native copy operations.

Usage:
    python benchmark_page_copy_ext.py

Output:
    - Performance comparison table
    - Speedup analysis
    - JSON results file
"""

import torch
import page_copy_ext
import time
import json
import argparse

def benchmark_copy(func, src, dst, num_iterations, warmup_iterations):
    """Benchmark a copy function"""
    
    # Warmup
    for _ in range(warmup_iterations):
        func(src, dst)
    
    # Synchronize
    torch.cuda.synchronize()
    
    # Benchmark
    start = time.time()
    for _ in range(num_iterations):
        func(src, dst)
    torch.cuda.synchronize()
    end = time.time()
    
    # Calculate metrics
    total_time = (end - start) * 1000  # ms
    avg_time = total_time / num_iterations  # ms
    num_elements = src.numel()
    element_size = src.element_size()
    total_bytes = num_elements * element_size * num_iterations
    throughput_gbps = (total_bytes / 1024**3) / (total_time / 1000)
    
    return {
        'total_time_ms': total_time,
        'avg_time_ms': avg_time,
        'throughput_gbps': throughput_gbps,
    }

def torch_copy(src, dst):
    """PyTorch native copy"""
    dst.copy_(src)

def torch_clone(src, dst):
    """PyTorch clone"""
    dst[:] = src.clone()

def custom_copy(src, dst):
    """Custom extension copy"""
    page_copy_ext.page_copy_(dst, src)

def run_benchmark(sizes, num_iterations=100, warmup_iterations=10):
    """Run benchmark across all sizes"""
    
    results = []
    
    print("=" * 80)
    print("  PyTorch Extension Benchmark")
    print("=" * 80)
    print()
    
    for size in sizes:
        print(f"Testing size: {size:,} elements ({size * 4 / 1024**2:.2f} MB)")
        print("-" * 80)
        
        # Create tensors
        src = torch.randn(size, device='cuda')
        dst = torch.zeros_like(src)
        
        # Benchmark PyTorch native
        torch_result = benchmark_copy(
            torch_copy, src, dst, num_iterations, warmup_iterations
        )
        
        # Benchmark PyTorch clone
        clone_result = benchmark_copy(
            torch_clone, src, dst, num_iterations, warmup_iterations
        )
        
        # Benchmark custom extension
        custom_result = benchmark_copy(
            custom_copy, src, dst, num_iterations, warmup_iterations
        )
        
        # Calculate speedups
        torch_speedup = custom_result['throughput_gbps'] / torch_result['throughput_gbps']
        clone_speedup = custom_result['throughput_gbps'] / clone_result['throughput_gbps']
        
        # Store results
        result = {
            'size': size,
            'size_mb': size * 4 / 1024**2,
            'torch_native': torch_result,
            'torch_clone': clone_result,
            'custom': custom_result,
            'speedup_vs_torch': torch_speedup,
            'speedup_vs_clone': clone_speedup,
        }
        results.append(result)
        
        # Print results
        print(f"  PyTorch native:  {torch_result['throughput_gbps']:8.2f} GB/s  "
              f"({torch_result['avg_time_ms']:.3f} ms)")
        print(f"  PyTorch clone:   {clone_result['throughput_gbps']:8.2f} GB/s  "
              f"({clone_result['avg_time_ms']:.3f} ms)")
        print(f"  Custom ext:      {custom_result['throughput_gbps']:8.2f} GB/s  "
              f"({custom_result['avg_time_ms']:.3f} ms)")
        print(f"  Speedup vs torch: {torch_speedup:.2f}x")
        print(f"  Speedup vs clone: {clone_speedup:.2f}x")
        print()
    
    return results

def print_summary(results):
    """Print summary table"""
    
    print("=" * 80)
    print("  Summary")
    print("=" * 80)
    print()
    
    print(f"{'Size (MB)':<12} {'Torch (GB/s)':<15} {'Clone (GB/s)':<15} "
          f"{'Custom (GB/s)':<15} {'Speedup':<10}")
    print("-" * 80)
    
    for result in results:
        print(f"{result['size_mb']:<12.2f} "
              f"{result['torch_native']['throughput_gbps']:<15.2f} "
              f"{result['torch_clone']['throughput_gbps']:<15.2f} "
              f"{result['custom']['throughput_gbps']:<15.2f} "
              f"{result['speedup_vs_torch']:<10.2f}x")
    
    print()
    
    # Calculate average speedup
    avg_speedup = sum(r['speedup_vs_torch'] for r in results) / len(results)
    print(f"Average speedup: {avg_speedup:.2f}x")
    print()

def save_results(results, output_file):
    """Save results to JSON file"""
    
    with open(output_file, 'w') as f:
        json.dump(results, f, indent=2)
    
    print(f"Results saved to: {output_file}")
    print()

def main():
    parser = argparse.ArgumentParser(description='Benchmark PyTorch extension')
    parser.add_argument('--sizes', type=str, default='1M,10M,50M,100M',
                        help='Comma-separated list of sizes (e.g., 1M,10M,50M)')
    parser.add_argument('--iterations', type=int, default=100,
                        help='Number of benchmark iterations')
    parser.add_argument('--warmup', type=int, default=10,
                        help='Number of warmup iterations')
    parser.add_argument('--output', type=str, default='benchmark_results.json',
                        help='Output JSON file')
    
    args = parser.parse_args()
    
    # Parse sizes
    size_map = {'K': 1024, 'M': 1024**2, 'G': 1024**3}
    sizes = []
    for size_str in args.sizes.split(','):
        size_str = size_str.strip()
        if size_str[-1] in size_map:
            size = int(float(size_str[:-1]) * size_map[size_str[-1]])
        else:
            size = int(size_str)
        sizes.append(size)
    
    # Check CUDA
    if not torch.cuda.is_available():
        print("ERROR: CUDA not available")
        return 1
    
    print(f"CUDA device: {torch.cuda.get_device_name(0)}")
    print()
    
    # Run benchmark
    results = run_benchmark(sizes, args.iterations, args.warmup)
    
    # Print summary
    print_summary(results)
    
    # Save results
    save_results(results, args.output)
    
    # Print tradeoffs
    print("=" * 80)
    print("  Tradeoffs")
    print("=" * 80)
    print()
    print("Pros of custom extension:")
    print("  - Direct kernel access")
    print("  - No Python overhead")
    print("  - Custom optimizations (coalescing, vectorization)")
    print("  - Educational value (learn CUDA + PyTorch integration)")
    print()
    print("Cons of custom extension:")
    print("  - Build complexity (requires CUDA toolkit)")
    print("  - Platform-specific (not portable)")
    print("  - Maintenance burden")
    print("  - PyTorch native is already highly optimized")
    print()
    print("When to use custom extension:")
    print("  - Specialized operations not in PyTorch")
    print("  - Performance-critical custom kernels")
    print("  - Learning/experimentation")
    print()
    print("When NOT to use custom extension:")
    print("  - Simple operations already in PyTorch (e.g., copy, clone)")
    print("  - Portability is important")
    print("  - Maintenance resources are limited")
    print()
    
    return 0

if __name__ == '__main__':
    import sys
    sys.exit(main())
