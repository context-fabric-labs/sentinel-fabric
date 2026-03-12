# Experiment Results Analysis Template

## Experiment [NUMBER]: [NAME]

### Date
YYYY-MM-DD

### Configuration
- **Duration**: ___ seconds
- **Warmup**: ___ seconds
- **RPS**: ___
- **Sessions**: ___
- **Pods**: ___

---

## Results Summary

### Strategy Comparison

| Metric | Strategy A | Strategy B | Improvement |
|--------|------------|------------|-------------|
| p50 Latency | ___ ms | ___ ms | __% ↓ |
| p95 Latency | ___ ms | ___ ms | __% ↓ |
| p99 Latency | ___ ms | ___ ms | __% ↓ |
| Throughput | ___ rps | ___ rps | __% ↑ |
| Error Rate | __% | __% | __% ↓ |
| Rejection Rate | __% | __% | __% ↓ |

### Key Findings

1. **[Finding 1]**: [Description with numbers]
   
   **Evidence**: [Metric values from results]
   
   **Why it matters**: [Explanation]

2. **[Finding 2]**: [Description with numbers]
   
   **Evidence**: [Metric values from results]
   
   **Why it matters**: [Explanation]

3. **[Finding 3]**: [Description with numbers]
   
   **Evidence**: [Metric values from results]
   
   **Why it matters**: [Explanation]

---

## Graphs

### Graph 1: [Title]

![Graph 1](graphs/graph1.png)

**Observation**: [What the graph shows]

**Insight**: [Why it matters]

### Graph 2: [Title]

![Graph 2](graphs/graph2.png)

**Observation**: [What the graph shows]

**Insight**: [Why it matters]

---

## Interview Talking Points

### Problem
[Describe the problem this experiment addresses]

### Solution
[Describe how the control plane feature solves it]

### Results
[Quantified improvement with numbers]

### Example Script

> "In experiment [N], we tested [feature] under [conditions].
> 
> We observed that [metric] improved by __%, from [baseline] to [improved].
> 
> This matters because [business impact - e.g., 'lower latency means better user experience'].
> 
> The key insight was [technical insight]."

---

## Raw Results

```json
{
  "strategy_a": {
    // Paste results.json content
  },
  "strategy_b": {
    // Paste results.json content
  }
}
```

---

## Next Steps

- [ ] Generate additional graphs
- [ ] Run follow-up experiment with [variation]
- [ ] Document findings in main README
- [ ] Add to interview deck

---

## Notes

[Any additional observations, issues, or ideas for future experiments]
