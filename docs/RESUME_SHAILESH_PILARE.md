# SHAILESH PILARE

## Sr. Staff / Principal System AI Engineer

San Jose, CA | [Phone] | [Email] | [LinkedIn] | [GitHub]

---

## PROFESSIONAL SUMMARY

**AI Systems Engineering leader with 15+ years of progressive expertise** (2011–2026) architecting production-grade AI/ML infrastructure at massive scale. Proven track record delivering **sub-100ms latency** for multi-model inference pipelines processing **2M+ requests/sec** across security, FinTech, and consumer AI domains.

**Career Impact:**

- **4.1× latency reduction** (350ms→85ms) at Broadcom Cloud SWG
- **10× throughput improvement** (1K→10K req/sec/GPU) via dynamic batching
- **3.7× faster inference** (8.5s→2.3s) for LLM workflows at Fiserv
- **Sub-5ms p99** at 24,500 TPS for fraud detection at CapitalOne
- **3.0× faster E2E** (850ms→280ms) for conversational AI at Apple

**Target Roles:** Sr. Staff / Principal / Distinguished Engineer — AI Infrastructure, GPU/Inference Platforms, HPC Systems, Low-Latency Distributed Systems

---

## DOMAIN EXPERTISE & TECHNICAL SKILLS

**AI/ML & HPC Infrastructure**

- Distributed GPU/CPU hosting platforms for LLMs, ensuring reliability and performance at enterprise scale. Applied advanced inference optimizations—quantization, paged attention, continuous batching, KV cache tuning—to reduce latency and cost. Built benchmarks, monitoring, and observability for multi-million-dollar GPU clusters, directly improving utilization and cost efficiency.
- **Technologies:** vLLM, Triton Inference Server, TensorRT-LLM, ONNX Runtime, TensorFlow Serving, PyTorch, CUDA, cuDNN, TensorRT
- **Optimization:** Quantization (INT8, FP16, FP8), paged attention, continuous batching, KV-cache tuning, CUDA Graphs, dynamic batching, GPU memory pooling
- **Monitoring:** NVIDIA DCGM, Nsight Systems/Compute, Prometheus, Grafana, custom dashboards, SLO/SLI tracking
- **Hardware:** NVIDIA T4, V100, A100, H100; NUMA / CFS / TLB bottleneck analysis, cost-aware scaling

**Low-Latency & Cloud-Native Systems**

- Expert in low-latency, cloud-native AI/ML systems, building and operating microservice-based platforms on AWS and GCP with focus on scalability, resiliency, compliance, and production reliability. Hands-on experience designing zero-copy architectures, shared-memory IPC, and GPU-accelerated inference pipelines using CUDA and TensorRT, delivering sub-5ms p99 latency at 24,500+ TPS. Proven ability to implement Kubernetes-based deployment patterns, including blue-green strategies, to support highly available, performant, and operationally robust distributed services.
- **Languages:** C++ (17/20), Rust, Python, Go, Bash, SQL, ARM NEON intrinsics, x86_64 (AVX2, AVX-512)
- **Cloud Platforms:** AWS (EKS, EC2, S3, Lambda), GCP (GKE, Compute Engine, Cloud Storage)
- **Container Orchestration:** Kubernetes, Helm, Docker, multi-cluster management, service mesh, cluster bring-up support
- **Infrastructure as Code:** Terraform, CloudFormation, Pulumi
- **CI/CD:** GitHub Actions, Jenkins, ArgoCD, canary deployments, blue-green strategies, automated rollbacks, production change controls
- **Zero-Copy Patterns:** Apache Arrow, shared memory IPC, memory-mapped files, RDMA, InfiniBand, RoCE
- **Networking:** eBPF/XDP, DPDK, TCP/IP tuning, gRPC (streaming RPC), REST, circuit breakers, rate limiting

**Agentic AI & RAG Systems**

- Building advanced LLM workflows using RAG patterns and multi-agent systems for multi-turn enterprise conversational AI. Built chatbots and agents that integrate vector databases, hybrid search, prompt tuning, memory, and semantic caching into production stacks, delivering low-latency, high-accuracy responses. Collaborate closely with product and platform teams to roll out these capabilities across multiple use cases while keeping them observable and governed.
- **LLM Orchestration:** Multi-agent systems, tool integration, memory management, RAG patterns, prompt tuning, semantic caching
- **Vector Search:** FAISS (HNSW), approximate nearest neighbor, embedding generation, hybrid search, vector databases (Pinecone, Weaviate, Milvus)
- **Model Architectures:** Transformers (BERT, DistilBERT, Conformer, FastSpeech), LLMs (8B–70B), XGBoost, CNN, RNN-T
- **Agentic Workflows:** Multi-agent orchestration, tool integration, memory management, RAG patterns for enterprise conversational AI
- **Data Integration:** Hybrid search, prompt tuning, memory systems, semantic caching for low-latency responses

**Distributed Data Systems & Platform Engineering**

- Design and build large-scale, cloud-native platforms for data-intensive applications, supporting high-throughput batch and real-time processing across distributed systems environments. Engineer scalable compute, storage, scheduling, and orchestration layers for analytics, AI/ML, and decisioning workloads using technologies such as Spark, Databricks, SQL/NoSQL, and Kubernetes-based resource management patterns inspired by systems like Apache YuniKorn. Implement platform-level capabilities for data reliability, metadata, governance, security, and observability to ensure compliant, resilient, and production-ready data lake and warehouse ecosystems.
- **Stream Processing:** Kafka, Flink, Spark Streaming, gRPC streaming RPC, event-driven services, pub/sub patterns
- **Batch Processing:** Spark, Databricks, Hadoop (HDFS, MapReduce)
- **Databases:** Redis, Cassandra, RocksDB, FoundationDB, PostgreSQL, MongoDB, Delta Lake, Iceberg
- **Data Warehouses:** Snowflake, BigQuery, data lakes, data lakehouses
- **Event-Driven:** Event sourcing, CQRS, change data capture, metadata management
- **Resource Management:** Kubernetes-based scheduling, Apache YuniKorn patterns, concurrency control, failover design
- **Platform Capabilities:** Data reliability, governance, security, observability, compliance (PCI-DSS, HIPAA, GDPR, SOC2, FedRAMP)

**Observability, Performance & Systems**

- Comprehensive monitoring, profiling, and optimization across the full stack—from application code to hardware infrastructure. Expert in Linux performance tuning, high availability design, and production troubleshooting at scale.
- **Metrics & Tracing:** Prometheus, Grafana, custom metrics exporters, OpenTelemetry, Jaeger, Zipkin, distributed tracing
- **Profiling & Testing:** NVIDIA Nsight Systems/Compute, DCGM, perf, eBPF, VTune, Instruments, load testing, replay and load testing, regression gates, chaos engineering
- **Diagnostics:** p50/p95/p99 latency analysis, queue-depth and backpressure diagnostics, bottleneck identification, latency SLO governance
- **Linux Systems:** Linux performance tuning, kernel parameters, cgroups, namespaces, real-time scheduling, HA clustering, bare-metal provisioning
- **Systems Administration:** UNIX administration, storage and networking fundamentals, NIC and server architecture awareness, NUMA topology, CPU/GPU memory hierarchies
- **High Availability:** Failover design, disaster recovery, service reliability, production incident response

---

## PROFESSIONAL EXPERIENCE

### **Fiserv** | San Jose, CA

**Principal Architect, AI Systems & Low-Latency Platforms** (Contract) | *Jan 2026 – Present*

*AI-powered loan processing platform orchestrating multi-model LLM inference for loan intake, underwriting, and decisioning workflows.*

**AI Systems Architecture:**

- Architected **multi-model LLM orchestration platform** using Rust and PyTorch, coordinating 8B classification models with 70B reasoning models for loan intake processing, achieving **3.7× latency reduction (8.5s→2.3s)** while maintaining 99.9% availability
- Designed **hierarchical large-context workflow engine** for borrower document processing (credit bureau, bank statements, tax returns, pay stubs), implementing intelligent context assembly that reduced token usage by **60%** while improving decision accuracy by **15%**
- Built **Rust-based control plane** with Axum/Tokio for health-aware routing, GPU-headroom scheduling, and explainable multi-factor scoring across 20+ distributed inference pods on AWS EKS

**GPU & Inference Optimization:**

- Implemented **zero-copy GPU transfer engine** in C++/CUDA with pinned memory, async DMA, and CUDA streams, achieving **3.2× faster H2D transfers** and 85% GPU utilization
- Designed **session-aware KV-cache management** with sticky routing, achieving **70% cache hit rate** for multi-turn loan workflows and reducing GPU memory pressure by 50%
- Integrated **vLLM** for continuous batching and PagedAttention, optimizing throughput for 500+ concurrent loan applications

**Reliability & Observability:**

- Established **admission control** with bounded queues, circuit breakers, and failure injection testing to harden mission-critical decisioning flows
- Built **Prometheus + OpenTelemetry** observability stack with distributed tracing across all inference stages, reducing latency debugging time by 50%
- Implemented **canary deployments** with automated rollback based on p99 latency and error rate thresholds

**Technical Stack:** Rust (Axum, Tokio, DashMap), Python (PyTorch, vLLM), C++/CUDA, Apache Arrow, Kubernetes (AWS EKS), Docker, Helm, Terraform, AWS (EC2, S3, EKS), NVIDIA H100 GPUs, vLLM, TensorRT-LLM, Prometheus, Grafana, OpenTelemetry, Linux

---

### **Capital One** | San Jose, CA

**Sr. Principal Engineer, Card AI Platform** | *Jan 2024 – Dec 2025*

*Three-tier agentic AI platform for real-time fraud detection processing 24,500+ transactions/sec with sub-5ms p99 latency.*

**AI Systems Architecture:**

- Architected **three-tier fraud detection platform** with deterministic Tier 1 scoring (<5ms p99), agentic Tier 2 reasoning (2–5s), and automated Tier 3 triage (5–10s), achieving **99.999% availability** while processing 24,500+ TPS
- Designed **per-core zero-copy scoring pipeline** in Rust with arena allocation, SPSC ring buffers, and cacheline-aligned feature blocks, eliminating cross-core synchronization overhead
- Built **ensemble scoring engine** combining XGBoost, GBDT, logistic scorecards, rule engines, and compact neural models reading from shared feature block with zero per-model serialization

**GPU & CUDA Optimization:**

- Implemented **CUDA Graphs** for neural scoring pipeline, eliminating 145µs of kernel launch overhead per inference (critical for 5ms p99 budget)
- Developed **micro-batching strategy** with preallocated pinned buffers and dedicated CUDA streams, achieving **10× throughput improvement** (1K→10K req/sec/GPU)
- Optimized **KV-cache memory layout** for fraud-scoring models, reducing GPU memory fragmentation and improving cache hit rate to 85%

**Sentinel Governance Framework:**

- Built **Sentinel gateway** with token admission control, priority lanes for high-value transactions (> $10K), and circuit breakers with DCGM integration for GPU health monitoring
- Implemented **Helios scheduler** with predictive latency modeling, queue management, and degrade modes (summary-only output) during 5× decline spikes
- Established **multi-tenant GPU isolation** with dedicated pools for premium customers, memory quotas, and time-slicing for fair scheduling

**Technical Stack:** Rust, C++ (17/20), Python (PyTorch, XGBoost), CUDA, Kubernetes (AWS EKS), Docker, Helm, Terraform, AWS (EC2, S3, EKS, Lambda), NVIDIA H100/A100 GPUs, vLLM, TensorRT-LLM, Apache Arrow, Kafka, Redis, RocksDB, gRPC, Prometheus, Grafana, OpenTelemetry, NVIDIA DCGM, Nsight Systems

---

### **Apple** | San Jose, CA

**Senior Staff Engineer, Siri Platform** | *Jun 2021 – Dec 2023*

*Multi-stage conversational AI platform for Siri processing millions of daily sessions with sub-300ms p99 end-to-end latency.*

**AI Systems Architecture:**

- Architected **multi-stage conversational pipeline** (ASR → NLU → Search → Orchestration → TTS) with **zero-copy shared memory arena**, reducing stage-to-stage overhead from 50–80ms to 5–10ms and achieving **3.0× E2E latency reduction (850ms→280ms)**
- Designed **streaming Conformer ASR** with incremental decoding and partial hypothesis propagation every 50–100ms, enabling real-time transcription as users speak
- Built **BERT-based NLU engine** with DistilBERT for intent classification (97% accuracy retention) and entity extraction with context-aware coreference resolution across multi-turn dialogues

**Performance Optimization:**

- Implemented **session-affine routing** with consistent hashing, achieving **85% session cache hit rate** and eliminating cold starts for conversational context
- Optimized **Apple Silicon inference** with NEON SIMD for audio feature extraction (4× speedup) and Metal GPU acceleration for transformer layers (2× speedup)
- Developed **backpressure control** with bounded queues between stages, preventing memory explosion during traffic spikes and maintaining 99.99% availability

**Streaming & Real-Time Systems:**

- Built **gRPC streaming layer** with zero-copy buffers and rate limiting for partial ASR results, reducing perceived latency by 200–300ms
- Implemented **FastSpeech neural TTS** with streaming synthesis and emotion-aware prosody control, delivering first audio chunk within 100ms of response decision
- Established **distributed tracing** across all 5 pipeline stages with correlated trace IDs, enabling rapid bottleneck identification

**Technical Stack:** C++ (17/20), Python (TensorFlow, PyTorch), gRPC (streaming RPC), Kubernetes (on-premise datacenters), NVIDIA T4/V100 GPUs, Apple Silicon (M1/M2), NEON SIMD, Metal Performance Shaders, BERT, DistilBERT, Conformer ASR, FastSpeech TTS, FAISS, Apache Arrow, Prometheus, Grafana, OpenTelemetry, Linux

---

### **Broadcom / Symantec** | San Jose, CA

**Principal Systems Engineer, Security Data Plane** | *Aug 2015 – Jun 2021*

*Cloud Secure Web Gateway with multi-model AI for malware detection, URL classification, DLP, and content analysis processing 2M+ requests/sec.*

**AI Systems Architecture:**

- Architected **unified AI inference platform** orchestrating 6+ ML models (malware detection, URL classification, DLP, content analysis, behavioral analysis, threat intelligence) with **dependency-aware parallelism**, reducing critical path from 360ms to 120ms
- Designed **zero-copy shared memory request context** accessible by all models without serialization, eliminating 200ms of per-stage overhead and achieving **4.1× E2E latency reduction (350ms→85ms)**
- Built **hybrid malware detection pipeline** combining XGBoost fast filter (95% of traffic, <5ms) with CNN deep analyzer (5% suspicious, 15–20ms on GPU), achieving 99.5% detection accuracy

**Model Development & Deployment:**

- Implemented **BERT + XGBoost ensemble** for URL classification into 50+ categories (phishing, gambling, social media), achieving 98.5% accuracy with <15ms p99 latency
- Built **NER + pattern matching DLP engine** for PII/PCI/PHI detection with 99.2% recall and <1% false positive rate, ensuring PCI-DSS, HIPAA, GDPR compliance
- Established **automated model deployment pipeline** with canary analysis (1% traffic for 24h), phased rollouts (10%→25%→50%→100%), and automated rollback, reducing deployment time from **2–4 weeks to 2 days**

**Hybrid Cloud & Multi-Tenancy:**

- Designed **hardware abstraction layer** for consistent inference across on-premise (customer datacenters) and GCP Cloud, detecting CPU/GPU/TPU and routing accordingly with AVX-512/CUDA/XLA optimizations
- Implemented **dynamic batching** for GPU throughput optimization (max batch 32–64, max latency 5–15ms), achieving **10× throughput improvement (1K→10K req/sec/GPU)**
- Built **multi-tenant GPU scheduling** with dedicated pools for premium customers, memory isolation, time-slicing, and priority queues during congestion

**Compliance & Observability:**

- Established **compliance auditing framework** with immutable audit trails for PCI-DSS, HIPAA, GDPR, SOC2, achieving 100% audit pass rate
- Built **Prometheus metrics** for per-model latency, per-tenant throughput, GPU utilization, and compliance blocks with Grafana dashboards
- Implemented **distributed tracing** with correlated request IDs across all 6 models for end-to-end visibility

**Technical Stack:** C++ (11/14/17), Python (TensorFlow, XGBoost, scikit-learn), CUDA, Kubernetes (GCP GKE), Docker, Terraform, GCP (Compute Engine, GKE, Cloud Storage), NVIDIA T4/V100 GPUs, TensorFlow Serving, ONNX Runtime, BERT (2018+), XGBoost, CNN, NER, FAISS, Kafka, Flink, RocksDB, Redis, eBPF/XDP, Prometheus, Grafana, Linux

---

### **IBM** | San Jose, CA

**Forward Deployment System Engineer, IBM BigInsights** | *Aug 2011 – Aug 2015*

*IBM BigInsights (flagship Hadoop platform, similar to Cloudera) - Deploying enterprise Hadoop clusters and contributing to backend product development.*

**Customer-Facing Platform Deployment:**

- Deployed and configured **IBM BigInsights Hadoop clusters** for Fortune 500 customers across finance, healthcare, and retail sectors, enabling large-scale data analytics and batch processing workloads
- Led **on-site installations** and performance tuning of HDFS, MapReduce, HBase, Hive, and Pig components, ensuring optimal cluster performance and reliability for mission-critical analytics
- Provided **technical consultation** to customers on Hadoop architecture, data pipeline design, and best practices for scaling from proof-of-concept to production deployments
- Conducted **training sessions** and knowledge transfer for customer teams on Hadoop ecosystem tools, troubleshooting, and operational best practices

**Backend Product Team Contributions:**

- Contributed to **IBM BigInsights product development** by implementing enhancements to Hadoop core components, including HDFS performance optimizations and MapReduce job scheduling improvements
- Developed **custom adapters and integrations** for enterprise data sources (databases, ETL tools, streaming systems), expanding BigInsights compatibility with customer infrastructure
- Participated in **beta testing programs** for new Hadoop features, providing field feedback to product teams and identifying edge cases from real-world deployments
- Created **automation scripts and tools** (Bash, Python) for cluster provisioning, monitoring, and troubleshooting, reducing deployment time by 40%

**Performance & Optimization:**

- Optimized **MapReduce job performance** through partition tuning, combiner strategies, and data locality improvements, achieving 2–3× speedup for customer workloads
- Implemented **cluster monitoring dashboards** using Ganglia, Nagios, and custom metrics to track HDFS health, node performance, and job completion times
- Troubleshot **production issues** including NameNode bottlenecks, DataNode failures, network partitioning, and resource contention in multi-tenant environments

**Technical Stack:** Hadoop (HDFS, MapReduce, HBase, Hive, Pig), IBM BigInsights, Java, Python, Bash, Linux, Ganglia, Nagios, MySQL, Apache ZooKeeper, Apache Flume, Apache Sqoop, enterprise data integration tools

---

## SELECTED PROJECTS & OPEN SOURCE

### **Sentinel Fabric** | *Personal Project* | *2024–Present*

- Open-source reference implementation of multi-model AI orchestration platform inspired by CapitalOne/Broadcom architectures
- **Rust control plane** with health-aware routing, GPU-headroom scheduling, and explainable scoring
- **C++/CUDA data plane** with zero-copy GPU transfers, CUDA Graphs, and async DMA
- GitHub: [your-github-url]

### **CUDA Kernel Labs** | *Personal Project* | *2023–Present*

- Educational repository for CUDA optimization techniques: dynamic batching, shared memory, tensor cores, CUDA Graphs
- Benchmark harnesses comparing naive vs. optimized implementations with Nsight profiling reports
- GitHub: [your-github-url]

---

## EDUCATION

**[Degree Name]** in **[Major]**
*[University Name]* | *[Year]*

**Relevant Coursework:** Distributed Systems, Computer Architecture, Machine Learning, Operating Systems, Parallel Computing

---

## CERTIFICATIONS

- **NVIDIA:** Deep Learning Institute (DLI) — CUDA Programming, TensorRT Optimization
- **AWS:** Certified Solutions Architect — Professional
- **Kubernetes:** Certified Kubernetes Administrator (CKA)
- **Security:** [Any relevant security certifications if applicable]

---

## PUBLICATIONS & TALKS

- **[Talk Title]** — *[Conference/Meetup Name]*, *[Year]*
  - Topic: AI Infrastructure, GPU Optimization, Low-Latency Systems
- **[Paper Title]** — *[Journal/Conference Name]*, *[Year]*
  - Topic: [Relevant to AI/ML systems]

---

## PATENTS

- **[Patent Title]** — *[Patent Number]*, *[Year]*
  - Description: [Brief description of invention related to AI/ML systems, security, or distributed systems]

---

## KEY ACHIEVEMENTS SUMMARY

| Company              | Period     | Key Metric             | Improvement                                      |
| -------------------- | ---------- | ---------------------- | ------------------------------------------------ |
| **Fiserv**     | 2026       | LLM Inference Latency  | 8.5s → 2.3s (**3.7× faster**)            |
| **Fiserv**     | 2026       | Token Efficiency       | 100% → 40% (**60% reduction**)            |
| **CapitalOne** | 2024–2025 | Fraud Scoring p99      | **Sub-5ms** at 24,500 TPS                  |
| **CapitalOne** | 2024–2025 | GPU Throughput         | 1K → 10K req/sec (**10×**)               |
| **Apple**      | 2021–2023 | Conversational E2E     | 850ms → 280ms (**3.0× faster**)          |
| **Apple**      | 2021–2023 | Session Cache Hit Rate | 40% → 85% (**2.1× improvement**)         |
| **Broadcom**   | 2015–2021 | Multi-Model E2E        | 350ms → 85ms (**4.1× faster**)           |
| **Broadcom**   | 2015–2021 | Model Deployment Time  | 2–4 weeks → 2 days (**10–20× faster**) |

---

## ADDITIONAL INFORMATION

- **Languages:** English (fluent), [Other languages if applicable]
- **Work Authorization:** [Your status]
- **Security Clearance:** [If applicable]
- **Volunteer/Mentorship:** [Any relevant mentorship, teaching, or volunteer work]
- **Interests:** Systems programming, GPU optimization, AI infrastructure, open-source contributions

---

**References available upon request.**
