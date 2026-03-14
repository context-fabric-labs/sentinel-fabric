# Transport Interface Design

## Overview

This document describes the minimal transport interface seam where future transport backends (UCX, NCCL, etc.) can attach.

## Design Goals

1. **Minimal**: Keep the interface small and focused
2. **Stable**: Avoid frequent changes to the interface
3. **Clear**: Obvious where to implement new transports
4. **Not Over-Engineered**: No complex plugin frameworks

## Interface Location

The transport interface is defined in:
```
include/pm_nixl/transport_interface.hpp
```

Key types:
- `ITransport` - Abstract base class for transports
- `TransportCaps` - Capability flags
- `TransportStats` - Performance statistics
- `TransportRegistry` - Simple factory registry

## Current Implementation

**CudaTransport** (`include/pm_nixl/cuda_transport.hpp`)
- Implements `ITransport` using CUDA copy operations
- Primary transport for single-node GPU systems
- Supports H2D, D2H, and D2D transfers

## Future Implementations

### UCX Transport (Example)

To add UCX support in the future:

1. Create `ucx_transport.hpp`:
```cpp
class UcxTransport : public ITransport {
public:
    std::string name() const override { return "UCX"; }
    
    TransportCaps capabilities() const override {
        return TransportCaps::HostToDevice |
               TransportCaps::DeviceToHost |
               TransportCaps::DeviceToDevice |
               TransportCaps::MultiNode |
               TransportCaps::Async;
    }
    
    Result<void> initialize() override {
        // Initialize UCX context and endpoints
    }
    
    Result<Completion> submit(...) override {
        // Use UCX put/get operations
    }
    
    // ... other methods
};
```

2. Register in main:
```cpp
TransportRegistry::register_transport("ucx", []() {
    return std::make_unique<UcxTransport>();
});
```

3. Use:
```cpp
auto transport = TransportRegistry::create("ucx");
transport->initialize();
transport->submit(dst, src, size);
```

### NCCL Transport (Example)

Similar pattern for NCCL:
```cpp
class NcclTransport : public ITransport {
    // Implement collective operations
};
```

## Why This Seam is Enough

1. **Separation of Concerns**: Transport mechanism is separate from higher-level logic
2. **Swappable**: Can change transports without changing callers
3. **Capability Negotiation**: Clear what each transport supports
4. **Statistics**: Performance monitoring built-in

## How to Avoid Overengineering

### DO:
- Keep `ITransport` minimal (6 core methods)
- Put transport-specific details in concrete classes
- Use simple factory pattern for registration
- Document where future transports would fit

### DON'T:
- Create complex plugin frameworks
- Abstract away all differences between transports
- Add every possible operation to the interface
- Make it configurable to the point of incomprehensibility

## Interface Methods

### Core Methods (Required)

```cpp
virtual std::string name() const = 0;
virtual TransportCaps capabilities() const = 0;
virtual Result<void> initialize() = 0;
virtual Result<void> shutdown() = 0;
virtual Result<Completion> submit(...) = 0;
virtual Result<void> synchronize() = 0;
```

### Optional Methods

```cpp
virtual Result<Completion> submit_d2d(...) {
    // Default: not supported
    // Override if transport supports D2D
}
```

### Statistics

```cpp
virtual TransportStats get_stats() const = 0;
virtual void reset_stats() = 0;
```

## Capability Flags

```cpp
enum class TransportCaps : uint32_t {
    None = 0,
    HostToDevice = (1 << 0),
    DeviceToHost = (1 << 1),
    DeviceToDevice = (1 << 2),
    P2P = (1 << 3),
    Async = (1 << 4),
    MultiNode = (1 << 5)
};
```

## Example Usage

```cpp
// Create transport
auto transport = TransportRegistry::create("cuda");
transport->initialize();

// Check capabilities
if (has_cap(transport->capabilities(), TransportCaps::DeviceToDevice)) {
    // Can do D2D transfers
}

// Submit transfer
auto completion = transport->submit(dst, src, size);

// Wait for completion
transport->synchronize();

// Get statistics
TransportStats stats = transport->get_stats();
std::cout << "Transferred " << stats.bytes_transferred << " bytes" << std::endl;

// Cleanup
transport->shutdown();
```

## Migration Path

To migrate from CUDA transport to UCX:

1. **Current Code**:
```cpp
TransferEngine engine;
engine.submit_h2d(...);
```

2. **With Transport Interface**:
```cpp
auto transport = TransportRegistry::create("cuda");
transport->submit(...);
```

3. **Future with UCX**:
```cpp
auto transport = TransportRegistry::create("ucx");
transport->submit(...);
```

Only the transport name changes - the rest of the code stays the same.

## Testing

Tests are in `tests/test_transport_interface.cpp`:
- Transport capabilities
- Transport initialization
- Transport statistics
- Transport registry
- Submit operations

## Next Steps

When implementing UCX or other transports:

1. Review this document
2. Implement `ITransport` interface
3. Add tests in `test_transport_interface.cpp`
4. Update this document with lessons learned
