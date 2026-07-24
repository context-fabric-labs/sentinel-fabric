#!/usr/bin/env python3
"""Estimate decoder-only transformer KV-cache capacity from Hugging Face config."""
import argparse
import sys

DTYPE_BYTES = {"fp32": 4, "float32": 4, "fp16": 2, "float16": 2,
               "bf16": 2, "bfloat16": 2, "fp8": 1}


def field(config, name):
    value = getattr(config, name, None)
    if value is None:
        raise ValueError("model config has no %s" % name)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, help="Hugging Face model ID or path")
    parser.add_argument("--dtype", default="bf16", choices=sorted(DTYPE_BYTES))
    parser.add_argument("--context-length", type=int, required=True)
    parser.add_argument("--concurrency", type=int, required=True)
    args = parser.parse_args()
    if args.context_length <= 0 or args.concurrency <= 0:
        parser.error("context length and concurrency must be positive")
    try:
        from transformers import AutoConfig
        config = AutoConfig.from_pretrained(args.model, trust_remote_code=False)
        layers = field(config, "num_hidden_layers")
        attention_heads = field(config, "num_attention_heads")
        kv_heads = getattr(config, "num_key_value_heads", None) or attention_heads
        hidden_size = field(config, "hidden_size")
        if hidden_size % attention_heads:
            raise ValueError("hidden_size is not divisible by num_attention_heads")
    except Exception as exc:
        print("error: could not load or interpret model config: %s" % exc, file=sys.stderr)
        return 2
    head_dim = hidden_size // attention_heads
    element_bytes = DTYPE_BYTES[args.dtype]
    bytes_per_token = 2 * layers * kv_heads * head_dim * element_bytes
    sequence_bytes = bytes_per_token * args.context_length
    total_bytes = sequence_bytes * args.concurrency
    gib = 1024 ** 3
    print("model: %s" % args.model)
    print("dtype: %s (%d bytes/element)" % (args.dtype, element_bytes))
    print("layers: %d" % layers)
    print("attention heads: %d" % attention_heads)
    print("KV heads: %d" % kv_heads)
    print("hidden size: %d" % hidden_size)
    print("head dimension: %d" % head_dim)
    print("KV bytes/token: %d (2 * %d * %d * %d * %d)" %
          (bytes_per_token, layers, kv_heads, head_dim, element_bytes))
    print("context length: %d" % args.context_length)
    print("concurrency: %d" % args.concurrency)
    print("KV GiB/sequence: %.4f" % (sequence_bytes / gib))
    print("total KV GiB: %.4f" % (total_bytes / gib))
    print("Note: theoretical payload only; vLLM blocks, workspaces, fragmentation, "
          "CUDA graphs, and runtime policy change observed capacity.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
