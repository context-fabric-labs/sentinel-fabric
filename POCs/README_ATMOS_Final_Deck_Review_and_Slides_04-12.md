# ATMOS customer presentation — review and presenter README

**Reviewed file:** `SNDK-ATMOS-Customer(1).pptx`  
**Purpose:** Help you present the final deck, especially slides 4–12, and identify a small set of changes before customer use.  
**Basis:** The uploaded PowerPoint’s 16 slides, rendered visuals, embedded chart data, and speaker notes. This is a review of that file, not an independent certification of product specifications or a new ATMOS benchmark.  
**PowerPoint status:** Not changed by this review. Rearrangements and wording changes below are recommendations.

> **Numbering:** Every slide number in this README means its actual position in the uploaded PowerPoint, starting with the cover as slide 1. Several printed footers and speaker-note headings still use numbers from earlier decks. For example, the current slide 5 says “16” in its footer. Ignore those inherited numbers when using this guide.

## Start here: the story you are presenting

**ATMOS is a proposed modular inference card that places a large HBF capacity tier beside NPU execution and working memory. The opportunity is to keep more useful model data near compute and reduce capacity-driven hardware or waiting. We still have to demonstrate that the complete service benefits.**

The presentation needs to answer these questions in order:

**What are we building? → How can it serve different model sizes? → What changes in deployment? → When would that improve the customer's service? → What should we validate together?**

Your current slides contain most of that story. The confusion comes from two things: the product-granularity explanation is late, and the charts mix different kinds of evidence without a strong transition.

### Slides 4–12 in one sentence each

| Actual slide | What it really answers | Kind of evidence |
|---|---|---|
| **4 — Eight compute-and-memory modules form one ATMOS card** | What is inside the proposed product? | Source design targets |
| **5 — Make the customer's time budget visible** | What time components do our example requests have? | Assumed service-time budgets |
| **6 — Compare a complete serving group—not just card capacity** | How could a large model's memory allocation differ? | Capacity arithmetic and a proposed placement |
| **7 — Residency helps when avoided waiting exceeds its own cost** | Under what assumptions does less waiting compensate for other costs? | Calculated sensitivity model |
| **8 — Choice Heatmap: Context Length vs Concurrency** | Which workload combinations should we investigate? | Illustrative platform-fit hypotheses, not calculated winners |
| **9 — Parameter Levers That Shift the Winner** | What workload characteristics can change the result? | Qualitative evaluation framework |
| **10 — Parallelism and Placement Change the Economics** | How does distributing computation differ from placing data in memory tiers? | Architecture explanation |
| **11 — Break-Even Surface: Nonresident Bytes vs Reuse** | How might nonresident data and reuse affect platform fit? | Illustrative hypotheses with an unresolved axis definition |
| **12 — Capacity Curve: Where the SLO Breaks** | How will we find the maximum load a service can sustain acceptably? | Schematic test-output example, not a measured load curve |

**Do not describe these nine slides as the results of one experiment.** They are a design description, several independent planning examples, and proposed evaluation views. In particular, slide 5 does not establish the latency of the slide 6 model; slide 7 does not generate the colored winners on slides 8 or 11; and slide 12 is not a measured consequence of any of them.

---

## 1. Recommended slide order

### Main customer presentation: 13 slides

This keeps the product first and the POC discussion last. All numbers in the “current slide” column refer to the uploaded file.

| New main position | Current slide | Role in the story |
|---:|---:|---|
| 1 | 1 | Introduce ATMOS and the capacity-led goal. |
| 2 | 2 | Explain the enterprise constraints. |
| 3 | 3 | Introduce HBF's intended place in the memory hierarchy. |
| 4 | 4 | Show the eight-module card. |
| **5** | **13** | Immediately explain one module, a module group, or multiple cards. |
| 6 | 6 | Make the large-model capacity example concrete. |
| 7 | 10 | Explain what coordination and placement remain inside the serving group. |
| **8** | **14** | State the customer value before further analytical detail. |
| 9 | 5 | Introduce request shapes and their time budgets. |
| 10 | 9 | Explain why workload shape changes the answer. |
| 11 | 7 | Show one transparent, calculable break-even example. |
| 12 | 15 | Invite discussion of the customer's actual workload and environment. |
| 13 | 16 | Close with the proposed next step. |

**Recommended order using current numbers:**

`1 → 2 → 3 → 4 → 13 → 6 → 10 → 14 → 5 → 9 → 7 → 15 → 16`

### Technical backup

| Backup | Current slide | Recommendation |
|---|---:|---|
| A1 | 8 | Retain as an evaluation map after relabeling or removing unsupported winner assignments. |
| A2 | 11 | Resolve footprint versus transfer volume before presenting its axes. Do not treat its platform labels as results. |
| A3 | 12 | Use to explain the intended load test. Remove the implied platform ranking until numerical results exist. |

Putting these slides in backup does not make unsupported assignments reliable. They still need the changes described below before being circulated as evidence.

For a technical workshop where all 16 slides are useful, the alternative order is:

`1 → 2 → 3 → 4 → 13 → 6 → 10 → 14 → 5 → 9 → 7 → 8 → 11 → 12 → 15 → 16`

### Why I would change the order

The strongest move is **current slide 13 immediately after current slide 4**. A customer should learn how modules become serving units before being shown a four-GPU versus one-card comparison.

Current slides 8 and 11 repeat the “platform fit changes with workload” message. Slide 7 makes the same discussion more defensible because it provides a formula and explicit assumptions. Use it as the main analytical exhibit rather than asking a customer to absorb three different heatmaps.

---

## 2. Changes I recommend before external use

### First priority: make the evidence unambiguous

| Item | What I found | Recommended change |
|---|---|---|
| **Slides 8 and 11: platform winners** | Blue, orange, and teal cells name specific platforms, but the final deck provides no per-cell experiment or calculation supporting those assignments. | Rename them as evaluation maps and use neutral test regions or “to validate” cells. Alternatively, replace the colors with actual calculated quantities such as logical KV bytes. |
| **Slide 12: platform capacity ranking** | The colored curves imply different sustainable arrival rates, but neither axis has numerical ticks and the deck supplies no results for these points. | Show a single generic saturation curve, or explicitly label the whole visual as a schematic with no platform ranking. Add measured platform curves later. |
| **Slide 5: time budgets versus performance** | 16, 40, and 120 seconds are planning means, not ATMOS measurements or p99 targets. No arrival-rate or request-mix sizing chart remains in this selected deck. | State “assumed LLM service times.” Remove or qualify the bottom reference to active-concurrency sizing unless that calculation is restored. |
| **Slide 6: capacity versus replacement ratio** | Four RTX cards and one ATMOS card are compared for weight residency and roughly similar logical KV budgets. | Keep the existing “not a 4× throughput or replacement claim” statement. Bring the reserve arithmetic into the notes you actually use. |
| **Slide 11: ambiguous quantity** | The axis says “nonresident bytes/request,” but the explanation calls it a “nonresident working set.” These are different quantities. | Choose one: warm working-set size in GB, or unique bytes actually fetched per request. Do not silently reinterpret the current label. |
| **Missing comparison definition** | RTX+CXL appears in the maps without a standalone description of the three configurations in this final file. | Add a short definition in the notes or a compact comparison key: resident RTX plus relevant offload, RTX plus qualified CXL staging, and modeled ATMOS. |

### Second priority: fix the merged-deck remnants

The final file combines two visible styles: warm SanDisk product slides and pale-blue “v3 workload-fit” slides. Keep the palette, but unify heading treatment, footers, evidence labels, and slide numbering. The source labels `I1`, `I3`, `D2`, `D5`, `D6`, and `D7` are useful internal references; a customer needs either a readable source register or a self-contained assumptions note.

Specific edits:

| Location | Recommended edit |
|---|---|
| Slide 2 | Replace the incomplete headline “More than compute, Enterprise AI Platform are constrained by” with **“Enterprise AI is constrained by more than compute.”** |
| Slide 3 | Shorten the long heading; in the reviewed rendering, it crowds the subtitle. Suggested title: **“HBF adds capacity near compute.”** Replace the absolute “How ATMOS solves…” eyebrow with **“How ATMOS aims to address…”** |
| Slide 3 | Clarify that “LPDDR / HBM” is a generic hierarchy label; the ATMOS concept on the right specifically names LPDDR. Do not imply both are fitted. |
| Slides 3 and 4 | Avoid repeating the same capacity and power targets. Keep the hierarchy on slide 3 and the product targets on slide 4. |
| Slide 5 | The small 2-second segment labels are crowded. Use totals above the bars and move tiny segment labels outside or into a small table. Define “activation waits”: model activation/loading or tensor activations are not the same thing. |
| Slide 7 | Standardize rounding. The formula gives **117.5**, not exactly 117, for +50% other-work time and 30% exposed waiting. Use one decimal consistently or round all cells with one explicit rule. |
| Slide 8 | An old `NO-FIT?` text object remains underneath the upper-right ATMOS overlay. The rendered cell reads ATMOS. Remove obsolete layered content so editing or export does not revive contradictory labels. |
| Slide 9 | Change **“Blocks all / no-fit”** to **“Weakens this HBF case / requires different proof.”** Low reuse or slow tools do not, by themselves, establish that every architecture is unusable. |
| Slide 10 | Change the vague EP statement about “experts churn” to a distinction between token dispatch, load imbalance, and actual weight-residency changes. Suggested wording appears in its walkthrough. |
| Slide 11 | The subtitle, horizontal-axis label, and some vertical-axis text crowd each other in the rendering. Separate the axes and simplify the labels. |
| Slides 14 and 16 | Replace ambiguous **“Watts per token”** with **“Joules per accepted token”** or **“Accepted tasks per kWh.”** Power divided by tokens/second is energy/token. |
| Slide 14 | Give “Tokens / rack” a time and quality boundary: for example, **“Accepted output tokens/s/rack.”** |
| Slide 15 | Remove “These questions combine the strongest discovery material from both presentations.” That is editing history, not customer content. |
| All slides | Renumber footers and notes. Current 4/5/6/7/13 have old footer numbers 03/16/13/15/04. Slides 8–12 also retain the “v3 workload-fit” badge. |

There is no need to rebuild the presentation from scratch. The product slides, the 250 GB example, the calculable break-even slide, and the close are useful material.

**Scope note:** This final 16-slide selection no longer contains the earlier dedicated sector-use-case pages or a numerical rack-demand example. That is acceptable for a general introduction. If the meeting is specifically about a sector or rack sizing, restore one relevant example rather than imply the surviving charts still establish those details.

---

## 3. Slide 4 — Eight compute-and-memory modules form one ATMOS card

**Current printed footer: 03. Source: actual slide 4 and its notes.**

### The idea in plain language

ATMOS is not being described as one undivided processor with one huge memory pool. The concept puts **eight compute-and-memory modules on one card**. Each module includes HBF, an NPU, and LPDDR working memory.

The proposed opportunity is to keep a larger portfolio of model weights near the compute resources that will use it.

### Walk through the picture

Start on the left: “These eight boxes are the modules that make up the card.”

Then explain the labels:

| Label | Meaning in this slide |
|---|---|
| **HBF** | The large model-capacity tier. |
| **NPU** | The local neural-compute engine. |
| **LPDDR** | Working memory for execution state; it is a different resource from HBF. |
| **~1 TB** | The card-level HBF capacity direction, aggregated across modules. |
| **128 GB** | The upper working-memory option used for this card illustration. The notes also identify 8–16 GB per module. |
| **200 W class** | A card planning budget, explicitly not measured operating power. |

Finish with the black strip: **aggregated capacity is not uniform shared memory**. Data placement and coordination still matter.

The notes normalize capacity calculations to 1,000 decimal GB rather than treating 8 × 128 GB and “1 TB” as an exact, universally interchangeable usable budget. Do not improvise a precise usable-capacity claim from the headline.

### What this slide establishes—and does not

It describes a product direction. It does not establish how many requests complete per second, how many isolated tenants the card supports, or whether every NPU can access every other module's memory with the same performance.

Current slide 13 is the natural next slide: it explains the proposed use of one module, a group of modules, or more than one card. Its own notes make isolation and supported runtime arrangements qualification items, not proven features.

### Say this in about 35 seconds

> “The proposed ATMOS card contains eight local compute-and-memory modules. HBF provides the large model-capacity tier, while the NPU and LPDDR provide the execution resources. That gives us a way to keep more model data near compute. These are design targets, not measured service results, and the total card capacity is distributed across modules. The next question is how the runtime should group those modules for small and large models.”

### Likely customer question

**“Does one terabyte mean the whole card behaves like one terabyte of GPU memory?”**

“No. The slide explicitly distinguishes aggregate capacity from uniform shared memory. We need a valid module-level model layout and a supported execution path.”

**Avoid:** “Eight modules mean eight fully isolated GPU-equivalent virtual machines.”

---

## 4. Slide 5 — Make the customer's time budget visible

**Current printed footer: 16. Source: actual slide 5, its native chart data, and its notes.**

### The idea in plain language

Before saying a service is fast or slow, define the request and where its time goes. This slide supplies **three illustrative LLM request shapes**. It does not compare RTX latency with ATMOS latency.

### What each bar contains

| Example request | Prefill | Decode | Exposed tier / launch | Total assumed service time |
|---|---:|---:|---:|---:|
| **4K input / 256 output tokens** | 2 s | 12 s | 2 s | **16 s** |
| **8K input / 512 output tokens** | 6 s | 28 s | 6 s | **40 s** |
| **32K input / 1,024 output tokens** | 20 s | 80 s | 20 s | **120 s** |

These are the actual values embedded in the chart. The sums are correct.

For presenting this chart, **prefill** means processing the input, **decode** means generating the response, and **exposed tier / launch** is the additional waiting category that the slide has allocated to data-tier or launch activity. The deck does not split that last category into individual causes.

The blue, burgundy, and amber colors identify time components—not platforms. Do not carry the platform-color interpretation from the later heatmaps into this chart.

### Why it matters to ATMOS

The proposed benefit is to reduce avoidable waiting associated with model/state availability or capacity-driven coordination. The slide does not establish that all amber time is removable, or that ATMOS improves the blue and burgundy components.

Queues, retrieval, OCR, external tools, and result writing are explicitly outside these bars. A 120-second LLM stage plus 10 seconds of serial tool work would be at least 130 seconds before the other omitted stages. That example also appears in the notes.

### What the numbers are not

They are not TTFT results, measured ATMOS times, p99 values, or customer-approved latency guarantees. The chart also does not provide the arrival rate or request mixture needed to turn these means into a concurrency estimate.

The implied illustrative decode rates are 256/12 ≈ **21.3**, 512/28 ≈ **18.3**, and 1,024/80 = **12.8 output tokens/s**. Those are simply arithmetic consequences of the chosen budgets—not platform forecasts.

### A detail to fix before tying this to the large-MoE example

“32K input / 1,024 out” would exceed a 32K total sequence limit if 32K means 32,768 input tokens. Slide 6's notes refer to proposed baseline contexts of at most 32K **including output**. The final deck does not reconcile these two labels.

Keep slide 5 explicitly model-independent, or specify a total retained-sequence budget and a compatible prompt/output split. Do not imply that all three bars were produced by the slide 6 checkpoint.

### Say this in about 40 seconds

> “These are example request budgets, not a platform benchmark. A short call might have 16 seconds allocated to its LLM stage, a medium call 40, and a longer analysis 120. We separate input processing, response generation, and exposed data or launch waiting. ATMOS aims to reduce avoidable state-related waiting, but the actual result depends on execution too. Retrieval, queues, and tools have to be added when we measure the complete workflow.”

**Avoid:** “ATMOS completes a 32K request in 120 seconds.”

---

## 5. Slide 6 — Compare a complete serving group—not just card capacity

**Current printed footer: 13. Source: actual slide 6 and its notes.**

### The idea in plain language

This is the clearest concrete product example in the deck. It asks: **where would the same large model and a similar logical KV budget live?**

It compares four RTX cards with one proposed eight-module ATMOS card. It does not compare their measured computation rates.

### Walk through the arithmetic

The slide assumes a **250 GB compiled model pack** for a 235B-class FP8 model. The notes explain that this is about 235 GB of raw one-byte parameters plus a provisional allowance. It is not a measured checkpoint size.

| Memory item | RTX illustration | ATMOS illustration |
|---|---:|---:|
| Model pack | 250 GB | 250 GB |
| Main weight location | GPU GDDR | HBF |
| Total working-memory capacity used in the illustration | 4 × 96 = **384 GB GDDR** | **128 GB LPDDR** |
| Reserve assumption | 10% × 384 = **38.4 GB** | **32 GB** outside the proposed KV budget |
| Logical KV budget | 384 − 38.4 − 250 = **95.6 GB** | 128 − 32 = **96 GB** |
| Separate HBF budget | Not part of this resident-GPU example | **900 GB** in the notes; a 250 GB pack fits within that budget |

The two reserve schemes are planning choices for different memory roles. They are not measured runtime overheads and need verification. The ATMOS reserve must cover whatever non-KV working state the actual implementation needs.

### What to point to on the slide

Point to the four GPU boxes: “The model and working state share this group’s GDDR budget.”

Point to the ATMOS modules: “Here the proposed model-capacity tier holds the weights, and LPDDR is budgeted separately for the active working set.”

Then immediately say: “The eight modules still need a valid model layout and coordination.”

### Important limitations

**Four is the illustrated group for the stated weight-plus-KV budget.** It is not proof that the weights alone require at least four GPUs under every precision, reserve policy, or offload arrangement.

The notes estimate roughly 15 full 32K logical KV sequences in either example, before extra duplication and other constraints. Do not turn that into a guaranteed concurrent-service result. Actual KV layout, rank duplication, allocator behavior, and model support must be established.

The two dual-GPU 2U nodes are an example packaging arrangement. The notes explicitly retain a larger GPU host as an alternative. Do not make the GPU control cross servers unnecessarily and then attribute that penalty to the accelerator architecture.

### Say this in about 45 seconds

> “This example holds the model pack at 250 GB and gives both arrangements roughly 96 GB of logical KV budget. The resident RTX arrangement combines four 96 GB cards. The proposed ATMOS arrangement keeps the weights in HBF and budgets working state separately in LPDDR. That may reduce the number of cards needed just for residency. It does not establish equivalent throughput: the NPU execution path, internal module coordination, and latency still have to be qualified.”

**Avoid:** “One ATMOS card replaces four RTX cards.”

### Best transition

“The model fits differently. Now let’s look at the coordination that remains.” This leads naturally to current slide 10.

---

## 6. Slide 7 — Residency helps when avoided waiting exceeds its own cost

**Current printed footer: 15. Source: actual slide 7 and its formula in the notes.**

### The idea in plain language

Reducing data waiting is useful only if it saves more time than the new architecture adds elsewhere.

This chart deliberately allows the candidate to be slower at the remaining work. It asks whether reduced exposed tier waiting could compensate. It is the strongest analytical chart because its cells can be reproduced.

### How to read the grid

| Part of the chart | Meaning |
|---|---|
| **Columns: 0%–50%** | Fraction of the reference service time spent waiting for addressable tier data. Not a cache miss percentage, cold-expert fraction, or percent of parameters. |
| **Rows: 0%, 10%, 25%, 50% slower** | Extra elapsed time required for the rest of the work on the candidate. A “25% slower” row uses 1.25 times the original non-waiting duration. |
| **Reference = 100** | A normalized time index. The original service duration is assigned 100. |
| **Cell below 100** | Lower service time under the stated assumptions. |
| **Cell = 100** | Equal modeled service time. |
| **Cell above 100** | Higher modeled service time. |

The chart assumes the eligible exposed waiting becomes **one-quarter** as long and adds overhead equal to **5% of the original total time**. “Four times less” means a 75% reduction of that particular waiting component, not elimination of all waiting or a fourfold model speedup.

### The formula

```text
candidate time / reference time = (1 − f) × c + f / 4 + 0.05

f = reference time fraction spent in addressable, exposed tier waiting
c = multiplier applied to the remaining work
```

“Addressable” matters: a slow external API call is not HBF waiting. “Exposed” matters: a transfer already hidden behind useful computation is not extra time to remove again.

### Explain the center cell with a 100-second teaching example

The 100 seconds here are a teaching version of the normalized index, not a benchmark.

| Component | Reference | Candidate assumption |
|---|---:|---:|
| Other work | 70 s | 70 × 1.25 = **87.5 s** |
| Exposed tier wait | 30 s | 30 / 4 = **7.5 s** |
| Added software overhead | — | **5 s** |
| **Total** | **100 s** | **100 s** |

That is why **30% waiting and +25% other-work time gives the break-even cell 100**.

Keep the same row and change the waiting fraction:

| Reference waiting fraction | Modeled candidate time index | Interpretation |
|---:|---:|---|
| 10% | **120** | Not enough eligible waiting to offset the costs. |
| 30% | **100** | Break-even. |
| 50% | **80** | Less service time under these assumptions. |

An index of 80 means 20% less modeled service time. It is not automatically 20% more service goodput, 20% fewer servers, or a measured energy saving.

### Are the 4× and 5% validated inputs?

No. The slide notes explicitly say they are chosen sensitivity inputs, not ratios derived from HBF/GDS peak bandwidth. The other-work penalties are also scenarios, not measured NPU performance.

The chart is valuable because it gives engineering a requirement to test. It does not establish where actual ATMOS hardware will land.

### Say this in about 50 seconds

> “This slide tests whether residency savings survive the rest of the system. Suppose the reference spends 30% of its time waiting for relevant data. If that wait falls to one-quarter, while the remaining work takes 25% longer and we add 5% overhead, the complete service only breaks even. Less initial waiting gives a loss; more can give a benefit. These are explicit assumptions. The measurement task is to replace them with the real execution and transfer profiles.”

**Avoid:** “We know ATMOS reduces waiting fourfold.”

### Arithmetic cleanup

Most displayed cells are rounded from the note formula. At +50% other work and 30% waiting, the exact index is **117.5**. The current slide displays **117**. Standardize rounding, preferably to one decimal for a technical appendix.

---

## 7. Slide 8 — Choice Heatmap: Context Length vs Concurrency

**Current badge: v3 workload-fit. Source: actual slide 8. No speaker notes are supplied for this slide.**

### The idea in plain language

It is an attempt to show that **larger requests and more simultaneous work can change the hardware arrangement worth investigating**.

The question is useful. The assigned platform winners are not established by this file.

### How to read it

The columns are 8K, 16K, 32K, 64K, and 128K context lengths. The rows are 8, 32, 64, 128, and 256 concurrent jobs. A cell names the platform the illustration suggests exploring for that combination.

For example, the visible cell at **32K and 128 concurrent jobs** says ATMOS. That is an illustrative assignment—not a demonstration that one card or one node can sustain that workload.

The slide does not specify the server count, model, KV format, request mix, arrival trace, or success threshold required to reproduce any cell. Its own subtitle says to replace the map with measured POC output.

### The important missing definition: “concurrent jobs”

A job can be actively executing, queued, or paused on a tool. Those are not automatically equivalent working-memory populations. Before using the map quantitatively, define whether the rows mean active sequences or all in-flight workflow jobs.

The 64K/128K columns also do not establish that the checkpoint discussed on slide 6 supports those lengths in the selected configuration. Those remain separate model/runtime experiments.

Likewise, slide 6's notes estimate roughly 15 full 32K logical KV sequences for its illustrated memory budget. The ATMOS label at 32K and 128 jobs on this map is not proof that the same single-card setup serves 128 active 32K sequences. A different configuration, sharing policy, or meaning of “jobs” would need to be stated and tested.

### Why I would not lead with this chart

The many teal cells look like an extensive measured ATMOS advantage even though the caption says illustrative. A small disclaimer is weaker than the visual impression.

Recommended title: **“Workload matrix to validate: context and active concurrency.”**

Recommended content: neutral cells for planned tests, or calculated memory quantities with named assumptions. Add platform winners only after results exist. Keep the original wording documented as a proposal, not as fact.

### Say this if you retain it as backup

> “This is the workload matrix we want to explore. We vary retained context and active concurrency to see where memory, execution, and queueing become limiting. The current platform labels are illustrative, not results. The customer’s model, topology, and service target will determine the actual boundaries.”

**Avoid:** “ATMOS wins at 128K and 256 jobs.”

**Editing note:** The upper-right rendered cell says ATMOS; an older `NO-FIT?` text object is underneath it. Remove that obsolete object when cleaning the slide.

---

## 8. Slide 9 — Parameter Levers That Shift the Winner

**Current badge: v3 workload-fit. Source: actual slide 9. No speaker notes are supplied for this slide.**

### The idea in plain language

There is no platform choice based on model size alone. The customer’s workload and constraints determine which arrangement deserves testing.

This slide works well as a discovery guide after you have explained the product.

### Translate the four columns

| Current column | What to say in customer language |
|---|---|
| **Pushes RTX/GPU** | “If the model stays resident and the response target is tight, test the established GPU path as a strong control.” |
| **Pushes RTX+CXL** | “If additional warm memory helps, compare a qualified CXL configuration and include its actual staging cost.” |
| **Pushes ATMOS/HBF** | “If a larger reusable portfolio cannot stay in the fast tier, test whether keeping it near compute reduces enough waiting or hardware cost.” |
| **Blocks all / no-fit** | “Some workloads weaken this particular HBF argument; some fail the application’s quality requirements regardless of hardware.” |

These are interpretations of the slide's qualitative categories, not measured rankings.

### Two wording qualifications

**Hot-expert reuse is not uniquely an HBF advantage.** Ask where the repeatedly used experts already reside. If the reference serves them from its resident fast-memory set, reuse does not by itself establish a need for HBF. This is consistent with slide 10's warning about workloads that fit in GDDR.

**Slow tools do not mean every accelerator is a bad fit.** They mean less of the end-to-end problem is necessarily addressed by faster inference or tier movement. Quality below the required threshold is a different issue: a result that is fast but unusable does not meet the service goal.

The phrase “real CXL device” is a comparison-validity requirement, not a workload characteristic. Keep that distinction clear.

### Say this in about 35 seconds

> “We want the right platform for the workload, not a blanket replacement claim. Stable resident models with tight response requirements give the GPU a strong starting point. Warm memory overflow makes CXL worth comparing. Large, reused data outside the fast tier creates the HBF question. If tools or quality dominate, we address those separately rather than claim memory capacity solves them.”

**Avoid:** “High reuse always favors ATMOS, and low reuse blocks every platform.”

### Useful customer question

“During your busy period, are you mainly waiting for computation, unavailable model/state data, communication, or external services?”

---

## 9. Slide 10 — Parallelism and Placement Change the Economics

**Current badge: v3 workload-fit. Source: actual slide 10, supplemented by the module-locality warning on slide 4 and the serving-group notes on slide 6.**

### The idea in plain language

The hardware comparison changes when we change **how computation is divided** and **where the data lives**.

The left-hand stack is a conceptual data classification. Its block heights are not percentages or capacity measurements. The lines pointing to the three boxes are not a measured network path.

### Explain the three boxes separately

| Box | Plain-language explanation |
|---|---|
| **Tensor parallelism** | Several devices cooperate on portions of the same computation. That can reduce the amount held by each device, but adds communication and synchronization. |
| **Expert parallelism** | Different devices own different experts. Work is dispatched to the selected expert owners and their results are combined. |
| **Tiered HBF placement** | An additional choice about where reusable weights/state reside and how they become available to execution. It can coexist with TP or EP. |

Tiered placement is **not a third mutually exclusive form of parallelism**. The slide should not be explained as “choose TP, EP, or HBF.”

### Explain the ATMOS implication

Slide 4 contains eight modules. Slide 6 says their internal coordination remains. Therefore “one complete model on one card” may still require cooperating modules.

The customer-facing message is to compare complete serving groups, including internal and external coordination—not simply count cards.

### Clarify “experts churn”

The current slide says EP is expensive when experts churn. That phrase is too broad.

Changing which expert a token selects is not the same as reloading expert weights. If the expert remains resident at its owner, that selection creates dispatch/combine work. Changing residency or moving an expert creates an additional weight-transfer problem.

**Suggested replacement:**

> “Places experts across devices. Performance depends on dispatch/combine cost, load balance, topology, and—when weights are tiered—residency changes.”

This is a recommended clarification of the slide, not an assertion that an ATMOS EP implementation is already qualified.

### Say this in about 45 seconds

> “There are two separate choices: how we divide the model’s computation and where we keep its data. Tensor parallelism shares tensor work; expert parallelism places experts on different owners. HBF adds a residency option that can coexist with either. For ATMOS, a complete model on one card still may coordinate across eight modules. We will compare the complete serving group and its communication rather than assume that fewer cards means no distributed work.”

**Avoid:** “One ATMOS card eliminates communication,” or “EP always reads changing experts from SSD.”

### Useful transition

“The layout changes where bytes move. The break-even question is whether it removes enough visible waiting to improve the service.” This leads to current slide 7 after the customer-value and workload discussion.

---

## 10. Slide 11 — Break-Even Surface: Nonresident Bytes vs Reuse

**Current badge: v3 workload-fit. Source: actual slide 11. No speaker notes are supplied for this slide.**

### The intended idea

The chart tries to identify a middle region: relevant data is too large for the fast tier, yet reused often enough that a closer capacity tier might be useful.

That is a useful hypothesis. The current chart does not calculate the colored platform assignments.

### Why this slide is difficult to explain as written

The vertical axis says **“Nonresident bytes / request”** and lists 20, 40, 80, 160, and 320 GB. The side explanation instead refers to a **“nonresident working set.”**

Those are different:

| Quantity | What it measures |
|---|---|
| **Nonresident working set** | How much relevant stored data is outside the selected fast-memory budget. |
| **Bytes actually fetched per request** | The physical data movement a request causes after reuse, batching, prefetch, and caching. |

The deck does not say which interpretation produced the rows. Do not choose one silently during the presentation.

The horizontal axis also mixes categories: “low/some/high reuse” describes reuse intensity, while “hot experts” and “hot + long KV” describe different workload conditions. It is not a numerical reuse scale.

### What you can safely explain now

“The chart illustrates the questions we would investigate as the nonresident population and reuse change.”

You cannot substantiate: “At 160 GB and high reuse, ATMOS is the best system.” No measured or calculated cell supports that conclusion here.

### Recommended treatment

Move this to backup and choose one of two future versions:

**Capacity-oriented version:** vertical axis = warm working-set size in GB; horizontal axis = a defined reuse or hot-cache-hit metric. Hold the hardware and workload constant.

**Traffic-oriented version:** vertical axis = unique bytes fetched per accepted request; horizontal axis = request rate or defined reuse. State the exact source/destination path and latency requirement.

The first examines residency economics. The second examines the traffic that can create bandwidth demand and queueing. They are related but should not be conflated.

### Say this in about 30 seconds

> “This is a candidate map of the warm-data opportunity, not a result. We need to separate how much data is outside fast memory from how much each request actually transfers. Once the workload, reuse metric, and hardware are fixed, measured results can tell us where a closer capacity tier is useful.”

**Avoid:** “A 320 GB model miss means every request transfers 320 GB.”

### Relationship to slide 7

Slide 7 calculates conditional **service time** from defined inputs. Slide 11 proposes **platform-fit regions** without that calculation. They are not equivalent break-even models, and one does not validate the other.

---

## 11. Slide 12 — Capacity Curve: Where the SLO Breaks

**Current badge: v3 workload-fit. Source: actual slide 12. No speaker notes are supplied for this slide.**

### The idea in plain language

A service may look good with a few requests but fail its response target when more work arrives. We want to find how much incoming work it can sustain without unacceptable latency, growing backlog, or failed output checks.

### Walk through the visual

The horizontal direction means more incoming work. The slide calls it **loan packages per hour**.

The vertical direction means worsening response behavior. The slide currently combines **p95 latency and queue pressure** in one label.

The red line represents a service boundary. Each colored line illustrates increasing pressure as load rises. The teal curve is drawn as crossing later—but that order is an illustration, not a result.

### What the chart cannot tell us

There are no numerical axis ticks or stated SLO value. The visual is built from diagram shapes, not an embedded numerical chart series. This deck does not establish the underlying arrival-rate/latency observations.

Therefore, you cannot read actual packages/hour, a latency value, a capacity ratio, or a customer rack count from it.

Do not infer that the vertical coordinate is simultaneously seconds and queue entries. Those are different measurements.

### Recommended measurement version

Use an explicitly defined main plot such as **offered workflow jobs/hour** versus **p95 end-to-end latency in seconds**, with the actual latency requirement marked. Report completed acceptable throughput, rejection rate, and backlog growth alongside it.

Keep queue depth as a separate plot or table. Count retries, timeouts, and errors rather than making a system look successful by dropping the hard requests.

For this generic deck, “workflow jobs/hour” is more reusable than “loan packages/hour.” Use the latter only after introducing that specific workflow.

Until results exist, use a single generic curve without naming a platform winner.

### Say this in about 35 seconds

> “This is the load-test result we want to produce, not a current benchmark. We increase incoming work and find where response time, quality, or backlog stops meeting the service requirement. The useful capacity is the sustainable rate before that failure point. We will replace this schematic with numerical results for each actual configuration.”

**Avoid:** “The teal curve proves ATMOS handles twice as many jobs.”

### What counts as success

Use the slide's own decision readout: test low, target, burst, and saturation traffic; report p95/p99, queue growth, correctness, and fallback behavior. The customer buys that service behavior, not the aesthetic spacing between curves.

---

## 12. One numerical connection you should not miss

### Slide 5 does not demonstrate slide 7's favorable region

This is a consistency check using the numbers already in the deck, not a new workload result.

In slide 5, the amber “exposed tier / launch” category is:

| Request shape | Amber / total | Share |
|---|---:|---:|
| Short | 2 / 16 | **12.5%** |
| Medium | 6 / 40 | **15.0%** |
| Long | 20 / 120 | **16.7%** |

Slide 7's +25% other-work-time scenario breaks even at **30% eligible exposed waiting**.

If—and only if—you deliberately apply that row to these request budgets, and assume the entire amber category is eligible for the stated 4× reduction, the indices would be approximately:

| Request shape | Conditional time index | Meaning |
|---|---:|---|
| Short | **117.5** | 17.5% more modeled service time |
| Medium | **115.0** | 15.0% more modeled service time |
| Long | **113.3** | 13.3% more modeled service time |

This does not show ATMOS loses those workloads: no actual execution multiplier or path improvement has been measured. It shows that **these independent illustrations cannot be chained into a demonstrated ATMOS speedup**.

Some launch time may not be eligible tier waiting at all. Conversely, a real workload might have different exposed movement. Measure the distinction rather than adjusting assumptions to force a favorable result.

**Recommended transition sentence:**

> “The previous chart defines example request budgets. This next chart is a separate sensitivity study showing the amount of addressable waiting needed under different execution assumptions; it is not a predicted improvement for those three requests.”

---

## 13. A five-minute explanation you can rehearse

### Product — use current slides 3, 4, and 13

> “ATMOS proposes a different allocation of model capacity and working memory. Each card contains eight local NPU, HBF, and LPDDR modules. The large HBF tier can retain model data without using the entire working-memory budget. The runtime design goal is to use a module for suitable small workloads or combine modules and cards for larger ones. Those allocation and isolation capabilities still need qualification.”

### Concrete example — use current slides 6 and 10

> “Our 250 GB model-pack example compares memory residency and about 96 GB of logical KV. It is not a four-for-one performance claim. The ATMOS group has internal coordination, and the GPU group must be evaluated on a fair OEM topology. We need to choose both the logical parallelism and the physical placement.”

### Service behavior — use current slides 5 and 9

> “We also define the request: input size, output size, and time components. A resident interactive model, a reusable document workload, and a tool-heavy workflow are not the same problem. We identify what is actually limiting the customer’s service rather than assuming more model capacity always helps.”

### Break-even — use current slide 7

> “Reducing exposed movement can compensate for other costs, but only beyond a threshold. In the illustrated case, a quarter of the original waiting plus 25% more other-work time and 5% overhead breaks even when the reference has 30% addressable waiting. The real task is to measure those inputs.”

### Close — use current slides 14, 15, and 16

> “The intended value is accepted work at a better infrastructure and operational boundary: throughput, latency, energy, footprint, and supportability. Once we understand your workload and current platform, we can agree a focused comparison and the evidence needed for the next step.”

The point is to tell one story. Do not narrate every cell of every heatmap.

---

## 14. Questions to practice before the customer meeting

| Customer question | Answer supported by this deck |
|---|---|
| **“Is the hardware available with these measured results?”** | “The figures shown are design targets and analytical examples. This deck does not provide measured ATMOS service results.” |
| **“What is the main differentiator?”** | “The proposed large near-compute model tier and modular execution arrangement. Its value must be shown in the complete service.” |
| **“One card replaces four GPUs?”** | “Slide 6 compares a particular residency and KV budget, not equivalent throughput or complete-system replacement.” |
| **“Does the card have one terabyte of uniform working memory?”** | “No. The slide distinguishes module-local resources and aggregate HBF capacity from working memory.” |
| **“Are the 16/40/120-second bars measurements?”** | “No. They are example service-time budgets for different request shapes.” |
| **“Where did the fourfold waiting reduction come from?”** | “It is a sensitivity assumption, not a silicon measurement or a peak-bandwidth ratio.” |
| **“Why does this heatmap say ATMOS wins at my scale?”** | “The current platform cells are hypotheses. We need your actual workload, configuration, and service target before assigning a result.” |
| **“What happens when an MoE token selects another expert?”** | “Separate dispatch to a resident owner from any additional weight-loading requirement. They are different data movements.” |
| **“How many jobs can one rack serve?”** | “This final selected deck does not contain a numerical rack-capacity calculation or the measurements needed to establish one.” |
| **“What do you want from the customer next?”** | “A representative workload, its service/quality requirements, the current platform, and the people who own that deployment decision.” |

---

## 15. Final rehearsal checklist

- [ ] Use actual PowerPoint positions or renumber the deck before rehearsal.
- [ ] Explain slide 4 and the current slide 13 together without implying guaranteed isolation or throughput.
- [ ] Reproduce the slide 6 memory arithmetic and say “residency comparison.”
- [ ] Explain the slide 7 center cell using 70 + 30 becoming 87.5 + 7.5 + 5.
- [ ] State that slide 5's bars, slide 7's model, and the winner maps are separate illustrations.
- [ ] Do not quote a platform winner, p99, or packages/hour from slides 8, 11, or 12.
- [ ] Define context, concurrency, nonresident bytes, and the metric being timed before discussing a chart quantitatively.
- [ ] Keep LLM time separate from retrieval, tools, queues, and end-to-end workflow time.
- [ ] Keep design target, assumed budget, analytical result, and measured result visibly distinct.
- [ ] Finish with the customer's workload and a practical next step—not an unsupported replacement ratio.

## Source and calculation notes

The source for the walkthroughs is **`SNDK-ATMOS-Customer(1).pptx`**, actual slides 4–12 and the notes supplied for slides 4–7. Current slide 13 supplies the module-allocation qualification; slides 14–16 supply the value and engagement close. Current slides 8–12 have no speaker-note explanations in this uploaded file.

The review uses the final deck's visible rendering where text extraction and layered objects differ. It does not import the longer V4 or V5 narrative as though those slides were still present.

The arithmetic checks are derived directly from the source chart and note inputs: the 16/40/120-second sums, their amber shares, the 384/95.6/96 GB budgets, and the slide 7 formula. Additional wording, slide order, and repair suggestions are explicitly editorial recommendations. No measured ATMOS, OEM, customer, queue, or concurrency result has been invented.

**The central message to retain:**

> **More model residency can change the deployment. Whether it improves the service depends on execution, data movement, coordination, and workload—not capacity alone.**
