# PyTorch C++ Extension Guide

## Overview

This guide explains how to build and use the PyTorch C++ extension for the page copy kernel.

## Prerequisites

1. **PyTorch with CUDA support**
   ```bash
   pip install torch --index-url https://download.pytorch.org/whl/cu118
   ```

2. **CUDA Toolkit 11.0+**
   ```bash
   nvcc --version
   ```

3. **C++17 compiler**
   ```bash
   g++ --version  # Should support C++17
   ```

4. **Python 3.8+**
   ```bash
   python --version
   ```

## Quick Start

### Build Extension

```bash
cd cuda-lab/pytorch_ext
python setup.py build_ext --inplace
```

### Test Extension

```bash
python test_page_copy_ext.py
```

### Run Benchmark

```bash
python benchmark_page_copy_ext.py
```

## Usage

### Basic Usage

```python
import torch
import page_copy_ext

# Create tensor
src = torch.randn(1000, device='cuda')

# Copy using extension
dst = page_copy_ext.page_copy(src)

# Verify
assert torch.allclose(src, dst)
```

### In-Place Copy

```python
import torch
import page_copy_ext

# Create tensors
src = torch.randn(1000, device='cuda')
dst = torch.zeros_like(src)

# In-place copy
page_copy_ext.page_copy_(dst, src)

# Verify
assert torch.allclose(src, dst)
```

### Get Extension Info

```python
import page_copy_ext

print(page_copy_ext.info())
print(f"Version: {page_copy_ext.__version__}")
```

## API Reference

### page_copy(src)

Copy tensor using optimized CUDA kernel.

**Args:**
- `src`: Source tensor (CUDA device, contiguous)

**Returns:**
- `dst`: Destination tensor (copy of src)

**Raises:**
- `RuntimeError`: If CUDA operation fails
- `TypeError`: If tensor is not on CUDA device
- `ValueError`: If tensor is not contiguous

**Example:**
```python
dst = page_copy_ext.page_copy(src)
```

### page_copy_(dst, src)

In-place tensor copy using optimized CUDA kernel.

**Args:**
- `dst`: Destination tensor (CUDA device, contiguous)
- `src`: Source tensor (CUDA device, contiguous, same shape as dst)

**Returns:**
- `dst`: Modified in-place

**Raises:**
- `RuntimeError`: If CUDA operation fails
- `TypeError`: If tensors not on CUDA device
- `ValueError`: If shapes/dtypes don't match

**Example:**
```python
page_copy_ext.page_copy_(dst, src)
```

### info()

Get extension information.

**Returns:**
- `str`: Extension info string

**Example:**
```python
print(page_copy_ext.info())
```

## Build Options

### Debug Build

```bash
DEBUG=1 python setup.py build_ext --inplace
```

### Clean Build

```bash
rm -rf build/ *.so
python setup.py build_ext --inplace
```

### Install System-Wide

```bash
pip install .
```

## Testing

### Run All Tests

```bash
python test_page_copy_ext.py
```

### Run Specific Test

```bash
python -c "import page_copy_ext; import torch; src = torch.randn(100, device='cuda'); dst = page_copy_ext.page_copy(src); print('Test passed!')"
```

## Benchmarking

### Run Benchmark

```bash
python benchmark_page_copy_ext.py
```

### Custom Sizes

```bash
python benchmark_page_copy_ext.py --sizes 1M,10M,50M,100M
```

### Custom Iterations

```bash
python benchmark_page_copy_ext.py --iterations 200 --warmup 20
```

### Save Results

```bash
python benchmark_page_copy_ext.py --output results.json
```

## Expected Performance

### Throughput Comparison

| Operation | Expected Throughput | Notes |
|-----------|-------------------|-------|
| PyTorch native | ~500-800 GB/s | Highly optimized |
| PyTorch clone | ~400-700 GB/s | Slightly slower |
| Custom extension | ~500-800 GB/s | Similar to native |

**Note:** For simple copy operations, PyTorch native is already highly optimized. The custom extension demonstrates the integration pattern but may not show significant speedup for this simple operation.

### When Custom Extension Helps

**Significant speedup expected:**
- Specialized operations not in PyTorch
- Custom memory access patterns
- Domain-specific optimizations
- Fused operations

**Minimal speedup expected:**
- Simple copy/clone (already optimized in PyTorch)
- Basic arithmetic (PyTorch uses cuBLAS/cuDNN)
- Common operations (PyTorch has years of optimization)

## Tradeoffs

### Pros

1. **Direct Kernel Access**
   - Full control over CUDA kernel
   - Custom optimizations possible
   - No Python overhead

2. **Educational Value**
   - Learn PyTorch extension development
   - Understand CUDA + Python integration
   - Hands-on experience

3. **Specialized Operations**
   - Implement operations not in PyTorch
   - Domain-specific optimizations
   - Custom memory layouts

### Cons

1. **Build Complexity**
   - Requires CUDA toolkit
   - Platform-specific builds
   - Version compatibility issues

2. **Maintenance Burden**
   - Keep up with PyTorch changes
   - CUDA version compatibility
   - Multiple platform support

3. **Limited Benefit for Simple Ops**
   - PyTorch native is highly optimized
   - Years of performance tuning
   - Hard to beat for common operations

### When to Use

**Use custom extension when:**
- Operation not available in PyTorch
- Performance-critical custom kernel
- Specialized memory access pattern
- Learning/experimentation

**Don't use custom extension when:**
- Simple operation already in PyTorch
- Portability is important
- Limited maintenance resources
- Performance difference is negligible

## Troubleshooting

### Issue: "CUDA not available"

**Solution:**
```bash
pip install torch --index-url https://download.pytorch.org/whl/cu118
```

### Issue: "nvcc not found"

**Solution:**
```bash
# Install CUDA toolkit
# Ubuntu: sudo apt install nvidia-cuda-toolkit
# Or download from: https://developer.nvidia.com/cuda-toolkit
```

### Issue: "Compilation error"

**Solution:**
- Check CUDA version compatibility
- Ensure C++17 support
- Check PyTorch version

### Issue: "Import error"

**Solution:**
```bash
# Rebuild extension
rm -rf build/ *.so
python setup.py build_ext --inplace
```

## Advanced Topics

### Multiple Kernels

To expose multiple kernels:

```cpp
// page_copy_ext.cpp
cudaError_t launch_kernel1(...);
cudaError_t launch_kernel2(...);

at::Tensor kernel1(at::Tensor src) { ... }
at::Tensor kernel2(at::Tensor src) { ... }

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
    m.def("kernel1", &kernel1, "Kernel 1");
    m.def("kernel2", &kernel2, "Kernel 2");
}
```

### Autograd Support

To add autograd support:

```cpp
class PageCopyFunction : public torch::autograd::Function<PageCopyFunction> {
public:
    static at::Tensor forward(
        torch::autograd::AutogradContext* ctx,
        at::Tensor src
    ) {
        // Forward pass
        return page_copy(src);
    }
    
    static std::vector<at::Tensor> backward(
        torch::autograd::AutogradContext* ctx,
        std::vector<at::Tensor> grad_outputs
    ) {
        // Backward pass
        return {grad_outputs[0]};
    }
};
```

### Multiple CUDA Streams

To use multiple streams:

```cpp
at::Tensor page_copy_async(at::Tensor src, cudaStream_t stream) {
    // Launch kernel with custom stream
    launch_page_copy_coalesced(dst_ptr, src_ptr, size, stream);
    return dst;
}
```

## Resources

- [PyTorch C++ Extensions](https://pytorch.org/tutorials/advanced/cpp_extension.html)
- [PyTorch Extension Library](https://github.com/pytorch/extension-cpp)
- [CUDA Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/)

## Summary

The PyTorch C++ extension demonstrates:
- ✅ How to expose CUDA kernels to PyTorch
- ✅ Build system integration
- ✅ Input validation and error handling
- ✅ Testing and benchmarking
- ✅ Tradeoff analysis

**Key takeaway:** Custom extensions are valuable for specialized operations, but PyTorch native is already highly optimized for common operations.
