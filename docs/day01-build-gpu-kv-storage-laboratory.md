# Day 1 — Build the GPU–KV–Storage Laboratory

## 1. Mission

Establish a measured GPU-only baseline before adding reuse or offload. Prefix caching and LMCache would otherwise hide the source of a latency or capacity change. First establish the physical path and its persistence boundaries: repository/EBS -> Linux page cache and DRAM -> PCIe -> L40S GPU-resident GDDR6 -> vLLM weights and KV state. The L40S has GDDR6, **not HBM**. This methodology nevertheless develops the same tiering discipline needed for HBM, DRAM, NVMe and a possible future HBF tier. HBF (high-bandwidth flash) remains a hypothesis requiring validation; it is not a proven HBM replacement, and this AWS NVMe lab exposes no SanDisk controller/NAND internals.

## 2. Target architecture

```text
HF/model repository -> EBS (persistent model cache) -> Linux FS/page cache -> host DRAM
                                                                     | PCIe
local NVMe (ephemeral: future KV tier) -> Linux FS/page cache -------+-> L40S GDDR6 hot tier
                                                                         -> vLLM: prefill -> KV blocks -> decode
```

Model weights are long-lived GPU allocations. Prefill computes K and V for prompt tokens; decode repeatedly reads that state and appends one token. PagedAttention manages KV in fixed blocks, avoiding contiguous-allocation pressure; it is not prefix caching. Prefix caching reuses matching prompt KV across requests. GQA has fewer KV heads than attention heads, reducing KV payload.

`KV bytes/token = 2 * layers * KV heads * head dimension * bytes/element`. Per-sequence bytes multiply by context; total bytes multiply by concurrent sequences. It is only an estimate: vLLM block allocation, alignment, fragmentation, workspaces, CUDA graphs, tensor parallelism and sliding windows alter observations.

## 3. Ten-hour execution schedule

| Time | Work |
|---|---|
| 00:00–00:25 | Meditation, sankalpa, choose one karma |
| 00:25–01:10 | Budget, tags, quota, security group |
| 01:10–02:00 | Create repo; launch EC2; short break |
| 02:10–03:10 | Identify storage and safely mount local NVMe |
| 03:10–04:00 | Capture topology; study KV formula |
| 04:00–04:45 | Meal break |
| 04:45–06:15 | Python/uv/vLLM/LMCache installation |
| 06:15–07:15 | Generate capacity matrix and launch baseline |
| 07:15–08:30 | Run five requests with telemetry |
| 08:30–09:30 | Write artifacts and compare calculation/logs |
| 09:30–10:00 | Review, copy results to EBS/S3, stop instance |

## 4. Morning practice

Meditate 15–20 minutes, then state for five minutes: “Today I will establish what the system actually is before deciding how it should be optimized.” The most-important karma is a reproducible no-reuse baseline. Pursue evidence before conclusion; do not try to prove expertise prematurely.

## 5. AWS controls and provisioning

In Billing > Budgets create a cost budget of `$1500`, period monthly, notifications at `$150`, `$375`, `$750`, `$1125`, and `$1350` (actual and forecast where available). Alerts do **not** stop resources. Tag every resource `Project=flashkv-fabric`, `Day=day01`, `Owner=<you>`, `AutoStop=manual`. Request the regional G-family quota before launch. Create a security group allowing TCP/22 only from your current public `/32`; create no 8000 ingress rule. Use `ssh -L 8000:127.0.0.1:8000 ubuntu@<instance>` for local access.

Use `us-west-2` unless price/capacity dictates otherwise: On-Demand `g6e.2xlarge`, current Ubuntu 22.04 Deep Learning Base GPU AMI, 250-GB gp3 root EBS, and local instance NVMe. Day 1 uses On-Demand because an interruption corrupts a baseline run and costs debugging time. Local NVMe is ephemeral; preserve code/results on EBS or S3 before stop/terminate.

For the active laboratory, use `us-east-1` because its G/VT quota is available. Treat the EC2 instance as disposable. Keep the repository, configuration, and final results on EBS-backed storage and copy completed artifacts to S3 where appropriate. Stop the instance after every session to eliminate GPU/vCPU cost; terminate it after the Day 1 session if the launch configuration and persistent artifacts are complete. Recreate later from the same AMI, tags, security group, instance profile, EBS configuration, and bootstrap commands. Never treat local instance NVMe as persistent across this lifecycle.

## 6. Sentinel Fabric workspace and host tools

Use the existing `sentinel-fabric` repository as the single source of code and results. Do not create or initialize a separate `flashkv-fabric` repository. Add Day 1 lab assets to the project while retaining its existing Rust control plane, C++/CUDA data plane, CUDA lab, and experiment structure:

```bash
cd sentinel-fabric
mkdir -p scripts benchmarks/{model_loading,kv_cache,storage} observability results/day01 architecture docs
git status
git add scripts benchmarks observability results architecture docs
git commit -m 'Add Day 1 FlashKV laboratory assets'
```

Keep the existing project README as the project entry point; add the Day 1 lab question to `docs/`: how do GPU-resident memory, DRAM and NVMe trade capacity, latency, endurance and cost while meeting serving SLOs? On the instance install: `sudo apt-get update && sudo apt-get install -y nvme-cli fio sysstat numactl pciutils jq git htop xfsprogs tree` (install `curl`/`wget` only if organizational security policy permits them).

## 7. Capture and identify hardware before any format

```bash
mkdir -p results/day01
{ date -Is; . /etc/os-release; uname -a; lscpu; numactl --hardware; nvidia-smi; nvidia-smi topo -m; lspci -tv; lsblk -o NAME,SIZE,MODEL,SERIAL,MOUNTPOINTS,TYPE; nvme list; df -h; free -h; } | tee results/day01/system-topology.txt
sudo nvme id-ctrl /dev/nvmeXn1 | less
```

Both EBS and instance disks can use NVMe names. `nvme id-ctrl` identifies EBS as Amazon Elastic Block Store and ephemeral storage as Amazon EC2 NVMe Instance Storage. **Never infer this from `/dev/nvmeXn1`; do not format until model, serial, size and mountpoint prove it is the intended unmounted instance store.**

After verification only (this destroys all data on the selected disk):

```bash
export FLASHKV_NVME_DEVICE=/dev/nvmeXn1   # replace only after verification
sudo mkfs.xfs -f "$FLASHKV_NVME_DEVICE"
sudo mkdir -p /mnt/nvme
echo "$FLASHKV_NVME_DEVICE /mnt/nvme xfs noatime,nodiratime 0 0" | sudo tee -a /etc/fstab
sudo mount -a && sudo chown ubuntu:ubuntu /mnt/nvme
mkdir -p /mnt/nvme/{models,hf-cache,kv-cache,results}
```

## 8. Environment, calculator, and baseline

From the `sentinel-fabric` repository root, use Python 3.12 where the selected AMI/package compatibility permits it; verify vLLM’s published compatibility first. `uv venv --python 3.12 .venv && source .venv/bin/activate && uv pip install vllm lmcache transformers huggingface_hub openai pandas psutil pynvml`. LMCache is installed but deliberately unconfigured. Save `python --version; python -c 'import torch,vllm; print(torch.__version__,vllm.__version__,torch.cuda.is_available()); print(torch.cuda.get_device_name(0))'; uv pip freeze > results/day01/python-packages.txt`.

Generate the matrix:

```bash
for c in 4096 8192 16384 32768; do for n in 1 4 8 16; do
  .venv/bin/python scripts/kv_capacity.py --model Qwen/Qwen2.5-7B-Instruct --dtype bf16 --context-length "$c" --concurrency "$n"
done; done | tee results/day01/kv-capacity-matrix.txt
```

Keep the HF cache on EBS for baseline persistence. Launch from the repo:

```bash
export HF_HOME="$HOME/.cache/huggingface"
.venv/bin/vllm serve Qwen/Qwen2.5-7B-Instruct --served-model-name flashkv-qwen7b --dtype bfloat16 --max-model-len 16384 --gpu-memory-utilization 0.90 --no-enable-prefix-caching --host 127.0.0.1 --port 8000 2>&1 | tee results/day01/vllm-baseline.log
```

Record download/load durations, weight/KV memory, GPU blocks, token capacity, estimated concurrency, and readiness timestamp from the log. These are runtime facts, not calculator outputs.

## 9. Benchmark and telemetry

Use the supplied `scripts/baseline_stream_benchmark.py`: `source .venv/bin/activate && python scripts/baseline_stream_benchmark.py`. It writes five unique-suffix streaming requests and TTFT/E2E JSONL. Check readiness through the SSH tunnel with the OpenAI client or `python -c 'from openai import OpenAI; print(OpenAI(base_url="http://127.0.0.1:8000/v1",api_key="x").models.list())'`.

In separate terminals during loading and inference: `nvidia-smi dmon -s pucvmet -d 1 > results/day01/nvidia-dmon.txt`, `iostat -xz 1 > results/day01/iostat.txt`, and `nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used,memory.total --format=csv -l 1 > results/day01/gpu-memory.csv`. Stop captures cleanly with Ctrl-C. Hypothesis: loading causes storage activity while steady inference has little model-file I/O after residency; measure it rather than assume it.

## 10. Artifacts, Sentinel Fabric integration, and review

Create `architecture/day01-hardware-topology.md` with AWS configuration, GPU/GDDR6, host memory/vCPU, EBS/local-NVMe device map and persistence, PCIe/NUMA, physical path, and bottleneck hypotheses. Create `architecture/day01-kv-capacity.md` with config (layers, heads, KV heads, head dimension, dtype), model estimate, matrix, vLLM observed blocks/capacity, calculated-versus-observed differences, safe context/concurrency, and unknowns.

The Day 1 server is a baseline external to the control-plane request path: do not alter scheduler behavior to optimize it. Preserve the existing `control-plane/` implementation and record the baseline endpoint, model name, and observed capacity in `docs/` or `architecture/`. Later days will connect this endpoint to the Sentinel Fabric control plane, add cache-aware telemetry in its existing observability components, and place reproducible workload definitions under `experiments/`.

Five questions: Why separate weights/KV? How does GQA reduce KV? How do EBS-NVMe and instance-NVMe differ? Why is load unlike steady decode? Why disable prefix reuse? Self-test: for a 48-GB GPU, measure weight/workspace headroom; calculate KV/token against context and concurrency distributions; inspect prefill/decode, GQA/MHA, vLLM blocks and prefix reuse; then compare CPU/NVMe restore cost and adopt admission control, session affinity, and SLO-goodput limits.

30-second executive report: “I established a reproducible path from persistent storage and local NVMe through Linux, DRAM and PCIe into GPU-resident model and KV state. I calculated theoretical KV requirements, compared them with vLLM’s runtime capacity, and captured a clean no-reuse latency baseline for later offload and routing experiments.”

## Completion checklist

- [ ] Budget/tags/quota/security configured
- [ ] GPU running; local NVMe verified and topology saved
- [ ] Calculator and matrix generated
- [ ] vLLM works with prefix cache disabled and LMCache disconnected
- [ ] Five latency runs plus GPU/I/O telemetry saved
- [ ] Both architecture artifacts written; results copied to EBS/S3
- [ ] Instance stopped (or terminated after EBS/S3 verification)
