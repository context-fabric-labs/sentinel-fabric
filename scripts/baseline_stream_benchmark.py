#!/usr/bin/env python3
"""Five-request streaming TTFT benchmark for the Day 1 local vLLM baseline."""
import json
import statistics
import time
from pathlib import Path

from openai import OpenAI

OUT = Path("results/day01/baseline-ttft.jsonl")
MODEL = "flashkv-qwen7b"


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    client = OpenAI(base_url="http://127.0.0.1:8000/v1", api_key="not-used")
    results = []
    with OUT.open("w") as output:
        for index in range(5):
            start = time.perf_counter()
            first_token_at = None
            text = ""
            try:
                stream = client.chat.completions.create(
                    model=MODEL,
                    messages=[{"role": "user", "content":
                               "In one sentence, explain KV cache. Unique run suffix: %d." % index}],
                    max_tokens=80, temperature=0, stream=True)
                for chunk in stream:
                    delta = chunk.choices[0].delta.content or ""
                    if delta and first_token_at is None:
                        first_token_at = time.perf_counter()
                    text += delta
                end = time.perf_counter()
                record = {"request": index, "ttft_s": round((first_token_at or end) - start, 4),
                          "e2e_s": round(end - start, 4), "output_chars": len(text), "ok": True}
            except Exception as exc:
                record = {"request": index, "ok": False, "error": str(exc)}
            print(json.dumps(record), flush=True)
            output.write(json.dumps(record) + "\n")
            results.append(record)
    successful = [r for r in results if r["ok"]]
    if successful:
        for key in ("ttft_s", "e2e_s"):
            values = [r[key] for r in successful]
            print(json.dumps({"metric": key, "mean_s": round(statistics.mean(values), 4),
                              "median_s": round(statistics.median(values), 4)}))


if __name__ == "__main__":
    main()
