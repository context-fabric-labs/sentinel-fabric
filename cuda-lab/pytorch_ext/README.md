# PyTorch Extension

This directory contains the PyTorch C++ extension for the page copy kernel.

## Files

- `page_copy_ext.cpp` - C++ extension code
- `setup.py` - Build script
- `test_page_copy_ext.py` - Tests
- `benchmark_page_copy_ext.py` - Benchmark script

## Quick Start

### Build

```bash
python setup.py build_ext --inplace
```

### Test

```bash
python test_page_copy_ext.py
```

### Benchmark

```bash
python benchmark_page_copy_ext.py
```

## Usage

```python
import torch
import page_copy_ext

# Copy tensor
src = torch.randn(1000, device='cuda')
dst = page_copy_ext.page_copy(src)
```

## Documentation

See `docs/PYTORCH_EXTENSION_GUIDE.md` for detailed documentation.
