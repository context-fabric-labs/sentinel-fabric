"""
PyTorch C++ Extension Build Script

This script builds the page_copy_ext PyTorch extension.

Usage:
    python setup.py build_ext --inplace

Or install:
    pip install .

Requirements:
    - PyTorch with CUDA support
    - CUDA Toolkit 11.0+
    - C++17 compiler
"""

from setuptools import setup
from torch.utils.cpp_extension import CUDAExtension, BuildExtension
import torch
import os

# Check CUDA availability
if not torch.cuda.is_available():
    print("WARNING: CUDA not available. Extension will not be built.")
    print("Please install PyTorch with CUDA support:")
    print("  pip install torch --index-url https://download.pytorch.org/whl/cu118")
    exit(1)

print(f"PyTorch version: {torch.__version__}")
print(f"CUDA version: {torch.version.cuda}")
print(f"CUDA available: {torch.cuda.is_available()}")

# Get CUDA home
cuda_home = os.environ.get('CUDA_HOME', None)
if cuda_home is None:
    # Try to find CUDA in common locations
    possible_paths = [
        '/usr/local/cuda',
        '/usr/local/cuda-11',
        '/usr/local/cuda-12',
        '/opt/cuda',
    ]
    for path in possible_paths:
        if os.path.exists(path):
            cuda_home = path
            break

if cuda_home:
    print(f"CUDA home: {cuda_home}")
else:
    print("WARNING: CUDA_HOME not set. Using PyTorch's CUDA.")

# Compiler flags
extra_compile_args = {
    'cxx': [
        '-O3',
        '-std=c++17',
        '-fPIC',
    ],
    'nvcc': [
        '-O3',
        '-std=c++17',
        '-arch=sm_60',  # Pascal and newer
        '-arch=sm_70',  # Volta
        '-arch=sm_80',  # Ampere
        '-arch=sm_90',  # Hopper
        '--expt-relaxed-constexpr',
        '-use_fast_math',
    ]
}

# Add debug flags if requested
if os.environ.get('DEBUG', '0') == '1':
    print("Building in DEBUG mode")
    extra_compile_args['cxx'].append('-g')
    extra_compile_args['cxx'].append('-O0')
    extra_compile_args['nvcc'].append('-g')
    extra_compile_args['nvcc'].append('-O0')
    extra_compile_args['nvcc'].append('-G')

# Extension definition
ext_modules = [
    CUDAExtension(
        name='page_copy_ext',
        sources=[
            'page_copy_ext.cpp',
            '../src/kernels/page_copy_coalesced.cu',
        ],
        include_dirs=[
            os.path.abspath('../src'),
            os.path.abspath('../src/kernels'),
        ],
        extra_compile_args=extra_compile_args,
        libraries=['cudart'],
    )
]

# Setup
setup(
    name='page_copy_ext',
    version='1.0.0',
    description='PyTorch C++ Extension for Optimized Page Copy Kernel',
    author='CUDA Lab',
    ext_modules=ext_modules,
    cmdclass={'build_ext': BuildExtension},
    python_requires='>=3.8',
    install_requires=[
        'torch>=1.8.0',
    ],
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
    ],
)
