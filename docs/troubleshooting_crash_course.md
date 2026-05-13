# HPC/AI Infrastructure Troubleshooting & Profiling Crash Course
## The 20% of Tools That Solve 80% of Problems

> This guide is organized as **4 stories** — each story is a real production incident
> that walks you through the troubleshooting methodology, tools, and resolution.
> Along the way, you'll learn the essential tools naturally.

---

# THE ESSENTIAL TOOLKIT (your 80/20 selection)

Before the stories, here's your focused toolkit — 12 tools out of 40+ that cover
~80% of HPC/AI infrastructure problems.

## Must-Have (your core 6)

| Tool | Category | What it answers |
|------|----------|----------------|
| **Nsight Systems (nsys)** | GPU profiling | "Where does GPU time go? What's the CPU↔GPU interaction?" |
| **Nsight Compute (ncu)** | GPU kernel analysis | "Why is THIS kernel slow? Compute vs memory bound?" |
| **Intel VTune** | CPU profiling | "Where does CPU time go? What's the hotspot? Cache misses?" |
| **perf** | CPU profiling (Linux) | "Quick CPU sampling, TLB misses, cache misses, branch misses" |
| **numastat / numactl** | NUMA analysis | "Is memory on the right NUMA node? Cross-NUMA traffic?" |
| **PyTorch Profiler** | ML framework | "Which PyTorch ops are slow? GPU vs CPU time per op?" |

## Must-Have Benchmarks (your core 4)

| Tool | Category | What it answers |
|------|----------|----------------|
| **STREAM** | Memory bandwidth | "Is my memory subsystem healthy? Bandwidth per NUMA node?" |
| **NCCL Tests** | GPU interconnect | "Is NVLink/InfiniBand performing correctly between GPUs?" |
| **FIO** | Storage | "Is my NVMe performing? IOPS, bandwidth, latency?" |
| **gpu-burn / DCGM diag** | GPU health | "Are all GPUs healthy? Any hardware issues?" |

## Quick-Check (your "first 60 seconds" tools)

| Tool | What you check |
|------|---------------|
| **htop** | CPU utilization per core, memory usage, process list |
| **nvidia-smi** | GPU utilization, memory, temperature, clock speeds |
| **free -h** | Total/used/available memory, swap usage |
| **numastat -m** | Per-NUMA memory allocation |
| **iostat -x 1** | Disk utilization, IOPS, latency |
| **ss -s** | Network socket statistics |

---

# THE METHODOLOGY: USE + RED

Before diving into stories, internalize these two frameworks.
They tell you WHAT to look for before you pick a tool.

## USE Method (Infrastructure — hardware/OS level)

For EVERY resource (CPU, memory, GPU, network, disk), check:

| Letter | Question | Example |
|--------|----------|---------|
| **U** — Utilization | "What % of this resource is busy?" | CPU: 95% utilized |
| **S** — Saturation | "Is work queuing because resource is full?" | CPU: runqueue > 1 per core |
| **E** — Errors | "Are there error events?" | GPU: ECC errors, NIC: packet drops |

## RED Method (Application — request/service level)

For EVERY service endpoint, check:

| Letter | Question | Example |
|--------|----------|---------|
| **R** — Rate | "How many requests/sec?" | vLLM: 60 QPS |
| **E** — Errors | "What % of requests fail?" | 0.1% HTTP 500 |
| **D** — Duration | "How long do requests take?" | P99: 1.2 sec |

---

# STORY 1: "The P99 Latency Spike That Wasn't a GPU Problem"
## Tools learned: htop, perf, numastat, Intel VTune, STREAM

### The incident

Monday morning. The fraud detection hot path (XGBoost + transformer, target P99 < 10ms)
suddenly shows P99 = 47ms. P50 is fine at 3.8ms. Only happening on worker-03.

### Step 1: The first 60 seconds (USE method scan)

```bash
# ═══ CPU: Utilization / Saturation / Errors ═══
ssh worker-03
htop
```

What htop shows:
```
  CPU[|||||||||||||||||||||||95.2%]   1/40   ← core 0: 95% (housekeeping core!)
  CPU[||||              18.3%]   2/40
  CPU[||||||||||||||||||86.7%]   5/40        ← isolated core, expected
  CPU[||||||||||||||||||84.1%]   6/40
  CPU[||||||||||||||||||88.9%]   7/40
  ...
  Mem[|||||||||||||||||||||22.1G/256G]
  Swp[                        0K/0K]
```

**First clue:** Core 0 (housekeeping core) is at 95%. That's unusual — housekeeping
cores should be at 20-30%. Something is consuming CPU on the housekeeping core.

```bash
# What's on core 0?
ps -eo pid,psr,pcpu,comm --sort=-pcpu | head -20
#  PID  CPU  %CPU  COMMAND
# 8842    0  45.2  prometheus-node   ← node_exporter eating CPU!
# 8901    0  32.1  telegraf          ← another monitoring agent!
# 1234    0  12.3  irqbalance        ← wait, irqbalance is running?!
```

**Problem found (partial):** irqbalance is running! We disabled it in our setup,
but someone re-enabled it during a maintenance window. It's fighting our manual
IRQ affinity and moving NIC interrupts to isolated cores.

```bash
# Verify IRQ affinity is broken
for irq in $(grep mlx5 /proc/interrupts | awk '{print $1}' | tr -d ':'); do
    echo "IRQ $irq: $(cat /proc/irq/$irq/smp_affinity_list)"
done
# IRQ 127: 0-39    ← BAD! Should be 0-3,20-23, now spread across ALL cores
# IRQ 128: 0-39    ← BAD!
```

**Root cause #1:** irqbalance redistributed NIC IRQs to isolated cores,
causing interrupt storms on latency-sensitive cores.

### Step 2: Deeper analysis with perf (check for hidden overhead)

```bash
# Quick TLB miss check on the fraud-scorer process
perf stat -e dTLB-load-misses,dTLB-loads,cache-misses,cache-references \
    -p $(pgrep fraud-scorer) -- sleep 10

# Output:
#  4,231,892  dTLB-load-misses  # 4.8% of all dTLB loads  ← HIGH (should be <0.5%)
# 88,164,521  dTLB-loads
#  2,891,234  cache-misses      # 12.3% of cache references ← HIGH
# 23,504,891  cache-references
```

**Problem found:** TLB miss rate is 4.8% (should be 0.3% with HugePages).
And L1 cache miss rate is 12.3% (should be ~2%).

```bash
# Check HugePages
cat /proc/meminfo | grep Huge
# HugePages_Total:     0     ← ZERO! HugePages are gone!
# HugePages_Free:      0
```

**Root cause #2:** Someone rebooted the node without the correct GRUB parameters.
HugePages were not allocated at boot.

### Step 3: Check NUMA with numastat

```bash
numastat -p $(pgrep fraud-scorer)
#                   Node 0    Node 1
# Heap              2048.00   6144.00   ← 75% of heap on WRONG NUMA node!
# Stack               16.00     0.00
# Private           1024.00   3072.00   ← also split across nodes
```

**Root cause #3:** Without HugePages (which we pre-allocated on NUMA 0),
the kernel's default allocation policy scattered memory across both NUMA nodes.
The fraud-scorer is pinned to NUMA 0 cores but 75% of its memory is on NUMA 1.
Every memory access crosses the NUMA boundary (+100ns per access).

### Step 4: Verify with Intel VTune (full picture)

```bash
# VTune hotspot analysis
vtune -collect hotspots -result-dir r001 \
    -target-pid $(pgrep fraud-scorer) -- sleep 30

# VTune memory access analysis (most relevant for NUMA issues)
vtune -collect memory-access -result-dir r002 \
    -target-pid $(pgrep fraud-scorer) -- sleep 30
```

VTune memory-access report shows:
```
Metric                              Value       Status
──────────────────────────────────  ──────      ──────
DRAM Bound:                         42.3%       HIGH
Local DRAM Access Ratio:            24.8%       POOR (should be >95%)
Remote DRAM Access Ratio:           75.2%       TERRIBLE
Average DRAM Latency:               187 ns      HIGH (local should be ~80ns)
TLB Miss Walk Duration:             2.1 ms/sec  HIGH
```

VTune confirms: 75% of DRAM accesses are remote (cross-NUMA), and TLB misses
are costing 2.1 ms per second of execution.

### Step 5: Baseline with STREAM benchmark

```bash
# Verify memory bandwidth is healthy (rules out hardware issue)
# Run STREAM on each NUMA node separately
numactl --cpunodebind=0 --membind=0 ./stream_c
# Triad: 120,000 MB/s   ← healthy for Xeon 8380

numactl --cpunodebind=1 --membind=1 ./stream_c
# Triad: 118,000 MB/s   ← healthy

# Cross-NUMA bandwidth (the penalty)
numactl --cpunodebind=0 --membind=1 ./stream_c
# Triad: 45,000 MB/s    ← 2.6x slower! This is what our fraud-scorer is hitting
```

STREAM confirms: cross-NUMA bandwidth is 2.6× worse than local.
The hardware is fine — the problem is memory placement.

### The fix

```bash
# Fix 1: Disable irqbalance (permanently this time)
systemctl disable irqbalance
systemctl stop irqbalance

# Restore manual IRQ affinity
for irq in $(grep mlx5 /proc/interrupts | awk '{print $1}' | tr -d ':'); do
    echo 0-3,20-23 > /proc/irq/$irq/smp_affinity_list
done

# Fix 2: Restore HugePages (add to GRUB permanently)
grubby --update-kernel=ALL --args="default_hugepagesz=2M hugepagesz=2M hugepages=8192"
# Reboot required for HugePages

# Fix 3: After reboot, verify NUMA locality
numastat -p $(pgrep fraud-scorer)
# Should show >95% on Node 0
```

### After fix:
```
P99 latency:         47 ms → 8.7 ms ✅
TLB miss rate:       4.8% → 0.3% ✅
Remote DRAM ratio:   75% → 2% ✅
IRQ distribution:    spread → pinned to housekeeping cores ✅
```

### Tools learned in this story:
- **htop**: first-look CPU utilization per core
- **perf stat**: quick TLB/cache miss check (30-second answer)
- **numastat**: NUMA memory placement verification
- **Intel VTune**: deep memory-access analysis with NUMA breakdown
- **STREAM**: baseline memory bandwidth per NUMA node

---

# STORY 2: "LLM Decode Is Slow But the GPU Looks Fine"
## Tools learned: nvidia-smi, Nsight Systems, Nsight Compute, NCCL Tests, PyTorch Profiler

### The incident

The warm path Llama 8B serving (vLLM on H100) is producing 6,500 tok/s throughput.
Expected: 12,000+ tok/s. nvidia-smi shows GPU utilization at "78%". Looks fine, right?

### Step 1: The first 60 seconds (nvidia-smi is misleading)

```bash
nvidia-smi
# GPU 0: H100 SXM 80GB
# Utilization: GPU 78%, Memory 45%
# Temperature: 62°C, Power: 450W
# Clock: 1980 MHz (max), Memory: 2619 MHz (max)
```

"78% utilization" seems healthy. But **nvidia-smi's utilization metric is misleading** —
it measures "% of time at least one kernel is running," NOT "% of peak performance."
A kernel that uses 5% of the GPU's compute still counts as "utilizing" the GPU.

### Step 2: vLLM metrics (RED method)

```bash
curl http://localhost:8000/metrics | grep -E "throughput|ttft|tpot|cache|preempt"
# vllm:avg_generation_throughput_toks_per_s    6,521
# vllm:time_to_first_token_seconds{p50}        0.042
# vllm:time_to_first_token_seconds{p99}        0.185
# vllm:time_per_output_token_seconds{p50}      0.0092    ← 9.2 ms/token
# vllm:time_per_output_token_seconds{p99}      0.0181    ← 18.1 ms/token!
# vllm:gpu_cache_usage_perc                     0.41
# vllm:num_preemptions_total                    0
```

**Clue:** TPOT P99 is 18.1ms (expected ~5-7ms for FP8 on H100).
KV cache is only 41% full, no preemptions. The problem is per-token decode speed.

### Step 3: Nsight Systems (find where time goes)

```bash
# Profile the vLLM server during steady-state serving
nsys profile -o decode_slow \
  --trace-fork-before-exec=true \
  --cuda-graph-trace=node \
  -t cuda,nvtx,cublas \
  --delay 30 --duration 30 \
  vllm serve meta-llama/Llama-3.1-8B-Instruct \
    --quantization fp8 --max-num-seqs 32 --port 8000

# In another terminal, send traffic
python benchmarks/benchmark_serving.py \
  --backend vllm --port 8000 \
  --num-prompts 50 --request-rate 30 \
  --random-input 500 --random-output 100
```

Open the `.nsys-rep` in Nsight Systems GUI. What you see:

```
Decode step timeline:

|--attention (0.8ms)--|--gap 12µs--|--gemm (0.4ms)--|--gap 15µs--|--norm (0.05ms)--|
|--gap 12µs--|--gemm (0.3ms)--|--gap 10µs--|--activation (0.02ms)--|--gap 12µs--|
... (30 more kernel launches)

Total kernel time per step: ~2.5 ms
Total gap time per step:    ~4.2 ms    ← GAPS ARE BIGGER THAN KERNELS!
Total step time:            ~6.7 ms
```

**Problem found:** CUDA Graphs are NOT enabled! Every decode step launches
~35 individual kernels with CPU gaps between them. The gaps (4.2ms) are
larger than the actual GPU compute (2.5ms).

```bash
# Check: is CUDA Graphs enabled?
curl http://localhost:8000/metrics | grep cuda_graph
# (no metrics — graphs are disabled)

# Check server logs
grep -i "eager\|cuda_graph" /var/log/vllm/server.log
# "enforce_eager=True"   ← CUDA Graphs are DISABLED!
```

**Root cause #1:** Someone set `--enforce-eager` during debugging and forgot
to remove it. CUDA Graphs are disabled.

### Step 4: Enable CUDA Graphs and re-profile

```bash
# Restart WITHOUT --enforce-eager (CUDA Graphs ON by default)
vllm serve meta-llama/Llama-3.1-8B-Instruct \
  --quantization fp8 --max-num-seqs 32

# Re-profile
nsys profile -o decode_with_graphs \
  --trace-fork-before-exec=true \
  --cuda-graph-trace=node \
  -t cuda,nvtx,cublas \
  --delay 30 --duration 30 \
  vllm serve ... --port 8000
```

New timeline:
```
|--CUDA_GRAPH_LAUNCH (2.8ms)--|--gap 3µs--|--CUDA_GRAPH_LAUNCH--|...

Total step time: ~3.0 ms (down from 6.7ms!)
```

But throughput only improved from 6,500 → 9,200 tok/s. Still below 12,000.

### Step 5: Nsight Compute (why is the attention kernel slow?)

```bash
# Profile the hot kernel
VLLM_ENFORCE_EAGER=1 ncu -o attn_analysis \
  -k regex:"flash_attn|paged_attention" -c 5 \
  --section SpeedOfLight \
  --section MemoryWorkloadAnalysis \
  --section WarpStateStats \
  python benchmarks/benchmark_latency.py \
    --model meta-llama/Llama-3.1-8B-Instruct \
    --quantization fp8 --input-len 500 --output-len 100 --batch-size 8
```

Nsight Compute report:
```
Kernel: paged_attention_v2_kernel
  SM Throughput:          14.2%     ← very low!
  DRAM Throughput:        82.7%     ← memory-bound (expected for decode)
  L2 Hit Rate:            31.4%     ← LOW!
  
  Warp Stall Reasons:
    long_scoreboard:      68.2%     ← waiting on global memory
    lg_throttle:          21.3%     ← too many outstanding loads
    not_selected:          8.1%     ← eligible but not picked
```

**Clue:** L2 hit rate is only 31.4%. For decode attention with KV cache,
this should be 50-70% if KV blocks have good locality. Low L2 hit rate
means KV cache blocks are scattered in memory.

### Step 6: Check KV cache dtype

```bash
curl http://localhost:8000/metrics | grep kv_cache_dtype
# (check server startup logs)
grep "kv_cache_dtype" /var/log/vllm/server.log
# kv_cache_dtype: auto   ← defaults to FP16 for this model
```

**Root cause #2:** KV cache is in FP16, not FP8. This means:
- 2× more memory per KV entry → KV blocks span more pages → worse L2 locality
- 2× more bandwidth consumed reading KV → slower decode

### Step 7: Check GPU interconnect (for TP scenarios)

```bash
# Run NCCL all-reduce benchmark (verify NVLink health)
# Even for single-GPU, this validates the GPU's memory subsystem

# For multi-GPU TP:
mpirun -np 4 /usr/local/bin/all_reduce_perf -b 8 -e 128M -f 2 -g 1
# Expected on NVLink 4.0: ~450 GB/s bus bandwidth
# If significantly lower → NVLink issue

# For single GPU, verify memory bandwidth:
# Use DCGM diagnostic
dcgmi diag -r 3 -j
# Level 3: stress test (PCIe bandwidth, memory bandwidth, compute)
```

NCCL tests show NVLink is healthy. DCGM shows no hardware issues.

### The fix

```bash
# Fix 1: Remove --enforce-eager (CUDA Graphs ON)
# Fix 2: Enable FP8 KV cache
vllm serve meta-llama/Llama-3.1-8B-Instruct \
  --quantization fp8 \
  --kv-cache-dtype fp8 \
  --enable-prefix-caching \
  --enable-chunked-prefill \
  --max-num-seqs 32
```

### After fix:
```
Throughput:          6,500 → 12,800 tok/s ✅ (nearly 2×)
TPOT P50:            9.2 ms → 4.8 ms ✅
TPOT P99:            18.1 ms → 8.2 ms ✅
L2 Hit Rate:         31.4% → 58.7% ✅ (FP8 KV = smaller blocks = better locality)
GPU gaps:            4.2 ms/step → 0.003 ms/step ✅ (CUDA Graphs)
```

### Tools learned in this story:
- **nvidia-smi**: first look (but DON'T trust utilization %)
- **vLLM metrics**: RED method (Rate, Errors, Duration)
- **Nsight Systems**: timeline view — find CPU gaps, kernel storms, graph behavior
- **Nsight Compute**: kernel-level — SOL analysis, memory workload, warp stalls
- **NCCL Tests**: verify GPU interconnect health
- **DCGM diag**: GPU hardware health check

---

# STORY 3: "Model Loading Takes 5 Minutes After Pod Restart"
## Tools learned: FIO, iostat, nvidia-smi (GDS), DCGM, gpu-burn

### The incident

Every time a fraud detection pod restarts (rolling update, node drain, crash),
the Llama 8B model takes **5 minutes to load** before the pod is ready.
During this time, requests queue at the gateway, causing P99 spikes for all users
on that node.

### Step 1: Where is the time going?

```bash
# Time the model loading
time vllm serve meta-llama/Llama-3.1-8B-Instruct --quantization fp8 &
# Watch the logs
tail -f /var/log/vllm/server.log

# Typical output:
# 00:00 Loading model weights...
# 02:30 Model weights loaded (150 seconds)       ← THIS IS THE BOTTLENECK
# 02:35 Initializing KV cache...
# 02:40 KV cache initialized
# 02:42 CUDA Graph capture (batch_size=1)...
# 03:00 CUDA Graph capture complete
# 03:00 Server ready
```

150 seconds for model weight loading. The model is 8B FP8 = ~8 GB.
8 GB in 150 seconds = ~53 MB/s. That's absurdly slow for NVMe.

### Step 2: Check storage performance with FIO

```bash
# Benchmark NVMe sequential read (what model loading does)
fio --name=seqread --ioengine=libaio --direct=1 \
    --bs=1M --iodepth=32 --rw=read --numjobs=4 \
    --size=8G --filename=/mnt/nvme0/test \
    --group_reporting

# Expected for Intel P5800X:
# READ: bw=6400MB/s, iops=6400
# Actual output:
# READ: bw=6380MB/s, iops=6380   ← NVMe is FINE!
```

NVMe is healthy at 6.4 GB/s. So why is model loading only 53 MB/s?

### Step 3: Check where the model is being loaded FROM

```bash
# Check vLLM model path
grep "model_path\|download" /var/log/vllm/server.log
# "Downloading model from huggingface.co..."   ← DOWNLOADING FROM INTERNET!
```

**Root cause #1:** The model is being downloaded from HuggingFace on every pod restart!
There's no local model cache on NVMe.

### Step 4: Fix — cache model on NVMe

```bash
# Pre-download model to NVMe
huggingface-cli download meta-llama/Llama-3.1-8B-Instruct \
  --local-dir /mnt/nvme0/model-cache/Llama-3.1-8B-Instruct

# Serve from local cache
vllm serve /mnt/nvme0/model-cache/Llama-3.1-8B-Instruct \
  --quantization fp8
```

New loading time: 150 seconds → **15 seconds** (8 GB at ~530 MB/s from NVMe).
But 15 seconds is still slow — NVMe can do 6.4 GB/s, we're only getting 530 MB/s.

### Step 5: Check the loading path with iostat

```bash
# Monitor disk I/O during model loading
iostat -x 1 /dev/nvme0n1

# During loading:
# Device  rrqm/s  r/s   rMB/s  await  %util
# nvme0n1  0.0    4200  525.0   0.24   38%    ← only 38% utilized!
```

NVMe is only 38% utilized during loading. The bottleneck is the CPU-side
processing (PyTorch weight deserialization, tensor creation, GPU transfer).

The path is: NVMe → CPU memory (read + deserialize) → GPU memory (cudaMemcpy).

### Step 6: Enable GPUDirect Storage (NVMe → GPU directly)

```bash
# Check if GDS is available
modprobe nvidia_fs
ls /dev/nvidia-fs*
# /dev/nvidia-fs0   ← GDS kernel module loaded

# Enable GDS in the application (requires compatible storage stack)
export CUFILE_ENV_PATH_JSON=/etc/cufile.json
```

With GDS: NVMe → GPU directly (bypass CPU memory).
Loading time: 15 seconds → **6 seconds** (8 GB at ~1.3 GB/s with GDS).

### Step 7: Validate GPU health (since we're troubleshooting)

```bash
# Quick GPU health check
gpu-burn 60
# Testing GPU 0: OK (temperature: 72°C, no errors)
# Testing GPU 1: OK
# ...

# Comprehensive DCGM diagnostic
dcgmi diag -r 3 -j
# Level 1: Quick health check (deployment)
# Level 2: Medium (stress test PCIe, memory)
# Level 3: Full (extended stress, ECC check, NVLink bandwidth)

# Check for ECC errors (silent data corruption)
nvidia-smi --query-gpu=ecc.errors.corrected.aggregate.total,\
ecc.errors.uncorrected.aggregate.total --format=csv
# 0, 0   ← clean
```

### Final configuration:

```yaml
# Pod spec with model cache on NVMe
volumes:
- name: model-cache
  hostPath:
    path: /mnt/nvme0/model-cache    # pre-populated via DaemonSet
    type: Directory

# Init container to verify model exists
initContainers:
- name: verify-model
  command: ["test", "-f", "/models/config.json"]
  volumeMounts:
  - name: model-cache
    mountPath: /models
```

### After fix:
```
Model loading:    5 min → 6 sec (50× faster)
Pod ready time:   5:20 → 0:25 (including KV cache + CUDA Graph capture)
Rolling update:   no user-visible impact (pod ready before traffic shifts)
```

### Tools learned in this story:
- **FIO**: benchmark storage performance (verify NVMe is healthy)
- **iostat**: real-time I/O monitoring during model loading
- **gpu-burn**: GPU stress test (verify hardware health)
- **DCGM diag**: comprehensive GPU diagnostics (PCIe, memory, NVLink, ECC)

---

# STORY 4: "Network Is the Bottleneck — Redis Feature Store Latency Spikes"
## Tools learned: ss, iperf3, tcpdump, perf (network stack), Intel VTune (microarchitecture)

### The incident

Redis feature store lookups are showing P99 of 3.2ms (target: <1ms).
P50 is fine at 0.5ms. The spikes correlate with traffic bursts.

### Step 1: Network health check

```bash
# Check network socket state
ss -s
# TCP:   4521 (estab 2340, closed 890, orphaned 12, timewait 1279)
#        ← 1279 TIME_WAIT sockets! Connection reuse issue?

# Check for packet drops
ss -s | grep drop
# TCPLostRetransmit: 234    ← retransmissions!

# Check interface errors
ip -s link show eth0
# RX errors: 0
# TX errors: 0
# RX dropped: 847          ← DROPS! NIC is dropping packets!
```

**Clue:** 847 RX drops. The NIC ring buffer is overflowing during bursts.

### Step 2: Check NIC ring buffer

```bash
# Current ring buffer size
ethtool -g eth0
# RX: 256    ← DEFAULT! Way too small for burst traffic
# TX: 256

# Check if we're actually dropping
ethtool -S eth0 | grep -i drop
# rx_out_of_buffer: 847    ← confirmed: ring buffer overflow
```

**Root cause #1:** NIC ring buffer is at default 256. During traffic bursts,
the kernel can't drain packets fast enough → overflow → drops → TCP retransmits
→ latency spikes.

### Step 3: Verify network bandwidth with iperf3

```bash
# On Redis server:
iperf3 -s

# On fraud-scorer node:
iperf3 -c redis-server -t 30 -P 4
# [ ID] Interval        Transfer    Bitrate
# [SUM] 0.00-30.00 sec  27.8 GBytes  7.95 Gbits/sec
```

Bandwidth is fine (7.95 Gbps on 10G link). The issue is latency during bursts,
not bandwidth saturation.

### Step 4: Check for interrupt coalescing issues

```bash
# Check interrupt coalescing settings
ethtool -c eth0
# rx-usecs: 50    ← NIC waits 50µs before raising interrupt
# rx-frames: 64   ← or 64 packets, whichever comes first

# For low-latency: reduce coalescing
ethtool -C eth0 rx-usecs 10 rx-frames 16
# Now: interrupt every 10µs or 16 packets
```

**Root cause #2:** Default interrupt coalescing (50µs) adds up to 50µs of latency
to every packet batch. At P99, this compounds with queue depth.

### Step 5: Deep analysis with perf (network stack profiling)

```bash
# Profile where CPU time goes in the network stack
perf record -g -p $(pgrep fraud-scorer) -- sleep 10
perf report

# Top functions:
# 23.4%  [kernel]  tcp_sendmsg
# 18.2%  [kernel]  tcp_recvmsg
# 12.1%  [kernel]  __netif_receive_skb_core
#  8.7%  [kernel]  mlx5e_handle_rx_cqe
#  6.3%  [kernel]  napi_gro_receive
```

The kernel network stack is consuming significant CPU. For our hot path,
this is overhead we'd eliminate with SR-IOV or AF_XDP (as discussed in stories).

### Step 6: Intel VTune microarchitecture analysis

```bash
# VTune microarchitecture exploration
vtune -collect uarch-exploration -result-dir r003 \
    -target-pid $(pgrep fraud-scorer) -- sleep 30
```

VTune shows:
```
Metric                              Value       Threshold
──────────────────────────────────  ──────      ─────────
Memory Bound:                       38.2%       HIGH
L1 Bound:                           5.1%        OK
L2 Bound:                           8.3%        MEDIUM
L3 Bound:                           12.4%       HIGH
DRAM Bound:                         12.4%       MEDIUM
Store Bound:                        0.0%        OK

Retiring:                           31.2%       LOW
Bad Speculation:                    12.8%       MEDIUM (branch mispredict)
```

The "L3 Bound" at 12.4% is unusual — it suggests the network stack's data structures
(sk_buff, socket buffers) are thrashing L3 cache. This is another reason
SR-IOV helps: it eliminates the kernel network stack entirely.

### The fix (incremental)

```bash
# Fix 1: Increase ring buffer (immediate, no restart)
ethtool -G eth0 rx 8192 tx 8192

# Fix 2: Reduce interrupt coalescing (immediate)
ethtool -C eth0 rx-usecs 10 rx-frames 16

# Fix 3: Enable busy polling (sysctl)
sysctl -w net.core.busy_read=50
sysctl -w net.core.busy_poll=50

# Fix 4: Increase TCP buffer sizes
sysctl -w net.core.rmem_max=16777216
sysctl -w net.ipv4.tcp_rmem="4096 87380 16777216"

# Fix 5 (Phase 2): Deploy SR-IOV for Redis traffic
# (eliminates kernel network stack entirely)
```

### After fix:
```
Redis P50:       0.5 ms → 0.4 ms (slight improvement)
Redis P99:       3.2 ms → 0.8 ms ✅ (4× improvement!)
RX drops:        847 → 0 ✅
Retransmits:     234 → 3 ✅
```

### Tools learned in this story:
- **ss**: socket statistics, connection state, drops
- **iperf3**: network bandwidth baseline
- **ethtool**: NIC ring buffer, interrupt coalescing, hardware stats
- **perf**: network stack CPU profiling
- **Intel VTune**: microarchitecture analysis (cache hierarchy, memory bound)

---

# QUICK REFERENCE: Which tool for which symptom

| Symptom | First tool | Second tool | What you're looking for |
|---------|-----------|-------------|------------------------|
| High P99 latency | htop + perf stat | VTune memory-access | TLB misses, NUMA locality, cache misses |
| Low GPU throughput | nvidia-smi + vLLM metrics | Nsight Systems | CPU gaps, missing CUDA Graphs, kernel storms |
| Slow kernel | Nsight Compute SOL | ncu WarpStateStats | Compute vs memory bound, stall reasons |
| Slow model loading | iostat + FIO | nvidia-smi (GDS check) | Storage bandwidth, GDS availability |
| Network latency spikes | ss + ethtool | iperf3 + tcpdump | Drops, ring buffer, retransmits, bandwidth |
| GPU errors | nvidia-smi -q | DCGM diag level 3 | ECC errors, temperature, clock throttling |
| NVLink issues | nvidia-smi topo | NCCL Tests | Topology, AllReduce bandwidth |
| Memory issues | free + numastat | perf stat (TLB) | HugePages, NUMA placement, TLB misses |

---

# BENCHMARKING CHEAT SHEET

Run these BEFORE deploying workloads to establish baselines:

```bash
# ═══ 1. Memory bandwidth (per NUMA node) ═══
numactl --cpunodebind=0 --membind=0 ./stream_c
numactl --cpunodebind=1 --membind=1 ./stream_c
# Expected: >100 GB/s per node for Xeon 8380+

# ═══ 2. Storage (NVMe health) ═══
fio --name=seqread --ioengine=libaio --direct=1 \
    --bs=1M --iodepth=32 --rw=read --numjobs=4 \
    --size=8G --filename=/mnt/nvme0/test
# Expected: >5 GB/s sequential read for datacenter NVMe

# ═══ 3. GPU health ═══
gpu-burn 60                    # 60-second stress test
dcgmi diag -r 2 -j            # DCGM medium diagnostic
nvidia-smi --query-gpu=clocks.current.graphics,\
clocks.current.memory,temperature.gpu --format=csv -l 1

# ═══ 4. GPU interconnect ═══
# All-reduce (measures NVLink bandwidth)
mpirun -np 8 /usr/local/bin/all_reduce_perf -b 8 -e 1G -f 2 -g 1
# Expected: >400 GB/s bus BW on NVLink 4.0

# ═══ 5. Network ═══
iperf3 -c <redis-server> -t 30 -P 4
# Expected: >9 Gbps on 10G link, >24 Gbps on 25G link

# ═══ 6. End-to-end inference ═══
python benchmarks/benchmark_serving.py \
  --backend vllm --model <model> \
  --num-prompts 200 --request-rate 50
# Establishes: throughput, TTFT, TPOT baselines
```

---

# THE 60-SECOND TRIAGE CHECKLIST

When something is wrong and you need to find the problem FAST:

```bash
# 1. CPU (5 seconds)
htop                           # which cores are hot? anything unexpected?

# 2. GPU (5 seconds)
nvidia-smi                     # utilization, memory, temperature, clocks

# 3. Memory (5 seconds)
free -h                        # swap usage? memory pressure?
numastat -m                    # cross-NUMA allocation?

# 4. Storage (5 seconds)
iostat -x 1 3                  # disk utilization, latency

# 5. Network (5 seconds)
ss -s                          # drops? retransmits? TIME_WAIT?

# 6. Application (5 seconds)
curl http://localhost:8000/metrics | grep -E "throughput|p99|error|cache"

# 7. Kernel (5 seconds)
dmesg -T | tail -20            # OOM? hardware errors? driver issues?

# Total: 35 seconds → you know which CATEGORY the problem is in
# Then pick the right deep-dive tool from the stories above
```
