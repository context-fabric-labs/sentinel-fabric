"""
PyTorch Extension Tests

Tests for the page_copy_ext PyTorch extension.

Usage:
    python test_page_copy_ext.py
"""

import torch
import page_copy_ext
import sys
import time

def test_extension_info():
    """Test extension info function"""
    print("Testing extension info...")
    info = page_copy_ext.info()
    print(info)
    print("✓ Extension info test passed\n")

def test_basic_copy():
    """Test basic tensor copy"""
    print("Testing basic copy...")
    
    # Create test tensor
    src = torch.randn(1000, device='cuda')
    
    # Copy using extension
    dst = page_copy_ext.page_copy(src)
    
    # Verify copy
    assert torch.allclose(src, dst), "Copy failed: tensors not equal"
    assert src.shape == dst.shape, "Copy failed: shapes not equal"
    assert src.dtype == dst.dtype, "Copy failed: dtypes not equal"
    
    print("✓ Basic copy test passed\n")

def test_inplace_copy():
    """Test in-place tensor copy"""
    print("Testing in-place copy...")
    
    # Create test tensors
    src = torch.randn(1000, device='cuda')
    dst = torch.zeros_like(src)
    
    # Copy in-place
    page_copy_ext.page_copy_(dst, src)
    
    # Verify copy
    assert torch.allclose(src, dst), "In-place copy failed: tensors not equal"
    
    print("✓ In-place copy test passed\n")

def test_various_shapes():
    """Test various tensor shapes"""
    print("Testing various shapes...")
    
    shapes = [
        (1,),              # Single element
        (100,),            # 1D
        (100, 100),        # 2D
        (100, 100, 100),   # 3D
        (10, 10, 10, 10),  # 4D
        (1000000,),        # Large 1D
        (1000, 1000),      # Large 2D
    ]
    
    for shape in shapes:
        src = torch.randn(shape, device='cuda')
        dst = page_copy_ext.page_copy(src)
        
        assert torch.allclose(src, dst), f"Copy failed for shape {shape}"
        assert src.shape == dst.shape, f"Shape mismatch for {shape}"
    
    print(f"✓ Tested {len(shapes)} different shapes\n")

def test_various_dtypes():
    """Test various data types"""
    print("Testing various dtypes...")
    
    dtypes = [
        torch.float32,
        torch.float64,
        torch.float16,
        torch.bfloat16,
        torch.int32,
        torch.int64,
        torch.uint8,
    ]
    
    for dtype in dtypes:
        try:
            if dtype in [torch.float16, torch.bfloat16]:
                src = torch.randn(1000, dtype=dtype, device='cuda')
            else:
                src = torch.randint(0, 100, (1000,), dtype=dtype, device='cuda')
            
            dst = page_copy_ext.page_copy(src)
            
            assert torch.allclose(src.float(), dst.float()), f"Copy failed for dtype {dtype}"
            assert src.dtype == dst.dtype, f"Dtype mismatch for {dtype}"
            
            print(f"  ✓ {dtype}")
        except Exception as e:
            print(f"  ✗ {dtype}: {e}")
    
    print("✓ Dtype tests complete\n")

def test_large_tensors():
    """Test large tensor copies"""
    print("Testing large tensors...")
    
    sizes_mb = [10, 50, 100]
    
    for size_mb in sizes_mb:
        num_elements = size_mb * 1024 * 1024 // 4  # float32 = 4 bytes
        src = torch.randn(num_elements, device='cuda')
        
        dst = page_copy_ext.page_copy(src)
        
        assert torch.allclose(src, dst), f"Copy failed for {size_mb}MB tensor"
        print(f"  ✓ {size_mb}MB tensor copied successfully")
    
    print("✓ Large tensor tests complete\n")

def test_error_handling():
    """Test error handling"""
    print("Testing error handling...")
    
    # Test CPU tensor (should fail)
    try:
        src_cpu = torch.randn(1000)
        page_copy_ext.page_copy(src_cpu)
        print("  ✗ CPU tensor should have failed")
        assert False, "CPU tensor should raise error"
    except RuntimeError as e:
        print(f"  ✓ CPU tensor correctly rejected: {str(e)[:50]}...")
    
    # Test non-contiguous tensor (should fail)
    try:
        src_noncontig = torch.randn(100, 100).t()  # Transpose makes non-contiguous
        page_copy_ext.page_copy(src_noncontig)
        print("  ✗ Non-contiguous tensor should have failed")
        assert False, "Non-contiguous tensor should raise error"
    except RuntimeError as e:
        print(f"  ✓ Non-contiguous tensor correctly rejected: {str(e)[:50]}...")
    
    # Test shape mismatch (should fail)
    try:
        src = torch.randn(100, device='cuda')
        dst = torch.zeros(200, device='cuda')
        page_copy_ext.page_copy_(dst, src)
        print("  ✗ Shape mismatch should have failed")
        assert False, "Shape mismatch should raise error"
    except RuntimeError as e:
        print(f"  ✓ Shape mismatch correctly rejected: {str(e)[:50]}...")
    
    print("✓ Error handling tests complete\n")

def test_correctness():
    """Test correctness with known values"""
    print("Testing correctness...")
    
    # Test with known pattern
    src = torch.arange(1000, dtype=torch.float32, device='cuda')
    dst = page_copy_ext.page_copy(src)
    
    expected = torch.arange(1000, dtype=torch.float32, device='cuda')
    assert torch.allclose(dst, expected), "Correctness test failed"
    
    # Test with all zeros
    src_zeros = torch.zeros(1000, device='cuda')
    dst_zeros = page_copy_ext.page_copy(src_zeros)
    assert torch.all(dst_zeros == 0), "All-zeros test failed"
    
    # Test with all ones
    src_ones = torch.ones(1000, device='cuda')
    dst_ones = page_copy_ext.page_copy(src_ones)
    assert torch.all(dst_ones == 1), "All-ones test failed"
    
    print("✓ Correctness tests complete\n")

def run_all_tests():
    """Run all tests"""
    print("=" * 60)
    print("  Page Copy Extension Tests")
    print("=" * 60)
    print()
    
    try:
        test_extension_info()
        test_basic_copy()
        test_inplace_copy()
        test_various_shapes()
        test_various_dtypes()
        test_large_tensors()
        test_error_handling()
        test_correctness()
        
        print("=" * 60)
        print("  All tests passed! ✓")
        print("=" * 60)
        return True
        
    except Exception as e:
        print("=" * 60)
        print(f"  Test failed: {e}")
        print("=" * 60)
        import traceback
        traceback.print_exc()
        return False

if __name__ == '__main__':
    success = run_all_tests()
    sys.exit(0 if success else 1)
