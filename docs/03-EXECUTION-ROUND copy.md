## PART 3 — EXECUTION ROUND (LinkedIn-Ready Edition)

---

### How This Document Is Organized

This document covers **all 40+ execution questions** across **7 situations** — each anchored to a real story from your portfolio. Three overlay layers have been added throughout to close the gaps for LinkedIn's specific evaluation:

1. **LinkedIn Values Overlay** — Every situation is tagged with the LinkedIn value it best demonstrates, with suggested phrasing.
2. **Scale Overlay ("At LinkedIn's Scale...")** — Each situation ends with a prepared bridge to LinkedIn's hundreds-of-millions-of-members, thousands-of-microservices scale.
3. **Search & Feed Domain Bridge** — Explicit connections from your fraud/Siri experience to LinkedIn's Search, Feed, and Recommendation systems.

Additionally, **Situation 7 (Mentorship / Prevent Burnout)** is a new addition covering team wellbeing, sustainable execution, and "Care About Each Other."

---

### Strategy

The 40+ execution questions are grouped into **7 situation types** — one per recurring situation you'll face. Each is structured as a nested arc: **Situation → Theme → Story → Action → Result → Impact → Questions**, anchored to one concrete story. Master these 7 and you cover every question.

| # | Situation | Theme | Anchor Story | Questions Covered | LinkedIn Value |
|---|---|---|---|---|---|
| **1** | Big projects with cross-org dependencies | Full SDLC / ownership | Capital One fraud platform (1-year rebuild) | Q1, Q2, Q5, Q6, Q7, Q19, Q32, Q37 | **Dream Big, Get Things Done, Know How** |
| **2** | Ambiguity / unclear requirements / difficult target | Define success, spike the risk | Siri HomePod redesign (1 quarter, 300 ms budget) | Q21, Q22, Q23, Q24, Q27, Q28, Q29 | **Know How** + **Members First** |
| **3** | Scope change / ownership | Re-scope, CR, push to v2 | Warm-path-for-every-transaction ask | Q8, Q9, Q10, Q15, Q26 | **Trust** (transparent tradeoffs) |
| **4** | Conflicts & stakeholder management | Present options, align on criteria | Guardrail-service disagreement with Cyber | Q20, Q30, Q31, Q33, Q34, Q35, Q36, Q38, Q39, Q40 | **One LinkedIn** + **Trust** |
| **5** | Feature vs. tech debt | Prioritize by business impact | GPU/observability debt vs. Identity-Fraud product | Q3, Q11, Q12, Q14 | **Dream Big, Get Things Done** |
| **6** | Bugs & reliability incidents / resource constraint | Contain → RCA → prevent, within budget | Cross-tenant leakage + vLLM customization | Q4, Q13, Q16, Q17, Q18, Q25 | **Members First** + **Trust** |
| **7** | Mentorship / Prevent Burnout | Protect the team, redistribute load, negotiate resources | Overloaded engineer: on-call + SPIKE + tech debt | Q41 (team wellbeing), Q42 (sustainable velocity) | **Care About Each Other** |

---

### The SARI Mental Model

Every execution answer follows **SARI** — Situation → Action → Result → Impact. Think of it as a funnel: you open by setting the stakes, narrow to _your_ decisions, prove they worked, then zoom back out to business value.

| Step | Purpose (one line) | What to cover | Staff-level signal | Trap to avoid |
|---|---|---|---|---|
| **S — Situation** | Set the stakes in 2–3 sentences | Scale, hard constraints, business pressure, _and the core conflict_ that made it hard | Quantify the starting state and name the tension (e.g., "35 ms p99 vs. a contractual sub-10 ms SLA at 24,500 TPS") | Long backstory; describing the system without saying why it mattered |
| **A — Action** | Show _your_ judgment, not team activity | The decision, the alternatives you weighed, the criteria you used, and how you drove execution | Speak in "I"; expose the tradeoff and the decision rule; show you considered 2–3 options | A list of activities; passive "we decided"; jumping to the answer with no alternatives |
| **R — Result** | Prove the action worked | The immediate, measurable change directly attributable to the action | Tie a before→after number to _your_ specific move | Vague "it improved" with no number |
| **I — Impact** | Connect to durable business value | Downstream effect on revenue/risk/cost/customers/team; what became _permanently_ better | Lead with the metric ($, %, risk, adoption) and show the compounding or org-level effect | Restating the Result; "the team was happy"; no quantification |

**Rule of thumb:** Situation is _one breath_, Action is _the bulk of the answer_, Result is _the proof_, Impact is _the mic-drop_. If you only remember one thing: **Impact must be quantified.**

---

### LinkedIn Values — Quick Reference for Weaving Into Answers

Use these natural phrases — don't force them, but weave one into every answer:

| LinkedIn Value | Natural Phrase to Weave In |
|---|---|
| **Members First** | "I asked: what's the member impact if we get this wrong?" / "The member experience drove my sequencing decision." |
| **Trust** | "I communicated early — at week 3, not week 7 — because trust requires no surprises." / "I made the rationale public so every team could challenge it." |
| **Care About Each Other** | "I noticed the load was unsustainable for one person, so I restructured before it became a burnout risk." / "Sustainable velocity matters more than heroic sprints." |
| **Dream Big, Get Things Done, Know How** | "The vision was ambitious — sub-5ms with LLM analysis — but I de-risked it with quarterly milestones that each delivered independently." |
| **One LinkedIn** | "I optimized for the whole org, not just my team — the certified-inline pattern was reused by 4 other services." / "Cross-team alignment was the real deliverable." |

---

### The Execution Operating Model (Vocabulary to Weave Into Every Answer)

SARI is _how you tell the story_. This operating model is _the machinery you ran underneath it_. Drop these terms into the **Action** step to signal staff-level execution discipline.

| Pillar | Sub-components | How to say it in an answer | Where it shows up |
|---|---|---|---|
| **Planning** | Program (KPI, Goal, Timeline) → Roadmap & Prioritization → OKR → Stories | "I framed the program around one KPI (sub-5 ms p99), set a quarterly roadmap, expressed each milestone as an OKR, and broke it into stories the team could pull." | Situation 1, 3 |
| **Dependencies** | DRI (Directly Responsible Individual) → Escalation Ladder | "Every cross-team dependency had a named DRI and a 3-step escalation ladder: DRI → eng-lead sync → VP review at 48 h." | Situation 1, 4 |
| **Incidents / Risk** | Impact (cost, timeline) → Resolution → RCA | "I sized the incident's impact in $ and schedule days, drove resolution with a tiger team, then ran a blameless RCA with action items tracked to closure." | Situation 6 |
| **Resourcing** | Capacity & skill matrix → Contingency → PTO calendar; Infra & Budget | "I mapped capacity against a skill matrix, built 30% contingency around the PTO calendar, and ring-fenced infra budget for the GPU bet." | Situation 6, 7 |
| **Scope change** | Impact → CR process → Stakeholder sign-off → Rebaseline | "New ask went through a change-request: I quantified impact, got stakeholder sign-off, and rebaselined the plan — no silent scope creep." | Situation 3 |
| **Communication** | Stakeholder map & channels → Weekly status → Monthly readouts | "I kept a stakeholder map with the right channel per audience: weekly written status for leads, monthly execution readouts for VPs." | Situation 1, 4 |
| **Quality & Readiness** | Test gates, CI benchmarks, launch readiness review, rollback plan | "Nothing shipped without passing CI latency benchmarks and a launch-readiness review with a tested rollback path." | Situation 3, 6 |
| **Team Health** | Workload audit → Rotation fairness → Skill growth → Burnout prevention | "I audit workload distribution monthly — on-call, SPIKEs, debt work — and redistribute before anyone becomes a single point of failure or burns out." | Situation 7 |

---

### SITUATION 1: Big Projects with Cross-Org Dependencies

**Theme:** Full SDLC ownership — own the program end-to-end, from north-star architecture to executive communication.
**LinkedIn Value:** **Dream Big, Get Things Done, Know How**
**Story:** ~1 year owning Capital One's fraud-detection platform rebuild — migrating a legacy Java stack (35 ms p99) to a three-tier Hot / Warm / Cold path architecture (Rust/CUDA hot path < 5 ms, 13B-LLM warm-path reasoning, 70B-LLM cold-path triage) at 24,500 TPS, across 5 engineering teams, against a contractual card-network SLA.

#### Action
- **North-star architecture with PRD + TDD** — wrote the product-requirements and technical-design docs; defined the hot/warm/cold tiers and the Arrow zero-copy contract between them _before_ any team built.
- **OKR, roadmap & prioritization** — one KPI (sub-5 ms p99), a quarterly roadmap, milestone-level OKRs, each tier an independently shippable increment.
- **Risk & dependencies; backup & contingency plan** — modeled cross-team dependencies as a DAG with a named DRI per edge and a 3-step escalation ladder; buffered the critical path 30% and kept an off-ramp at every milestone.
- **Epics & stories** — broke each tier into epics and pull-able stories mapped to roadmap themes.
- **Daily standup; bi-weekly / monthly status** — blockers-only daily sync across teams; bi-weekly written status for leads, monthly readout for directors.
- **Executive communication** — one-page monthly execution readout (milestones, $ impact, decisions needed) with explicit continue/adjust/stop calls.

#### Result
- Each quarter shipped an **independently valuable milestone** — Hot, then Warm, then Cold path — each usable on its own.
- **Five teams stayed aligned on stable interface contracts; no milestone slipped the critical path.**

#### Impact
- **Cut infra cost to ~1/3 → ~$20M/year saved.**
- **~$45M total savings in the first year.**
- **Fewer production incidents and faster triage** (p99 35 ms → 3.8 ms at 24,500 TPS).

#### 🔗 LinkedIn Search/Feed Bridge
> "At LinkedIn, Feed ranking pipeline modernization or Search relevance improvements follow the same pattern: multi-team dependencies (ranking, feature store, serving, ML platform), contractual latency SLAs, and quarterly milestones that each improve a user-facing metric. The dependency DAG approach — with DRIs per edge and interface contracts defined upfront — scales directly. The difference is scale: at LinkedIn, I'd be coordinating across thousands of microservices and hundreds of millions of members, which means the interface contracts and CI gates become even more critical because you can't manually verify anything at that scale."

#### 🎯 Bar Raiser Deep-Probe Preparation
**Probe:** "You said 5 teams stayed aligned — what happened when they didn't?"
**Answer:** "The KG team at Apple slipped on entity embeddings. I shrank the ask to a static snapshot, loaned an engineer, and unblocked in 5 days. The escalation ladder was ready but we resolved at the working level. At Capital One, one team challenged my prioritization — they had valid new data, so I reprioritized. Holding firm when right and adjusting when wrong are both signs of good judgment."

**Probe:** "What would you do differently?"
**Answer:** "I'd invest earlier in automated dependency health checks — at Capital One we caught slips in weekly syncs, but at LinkedIn's scale you'd need automated signals: if Team X's API contract test fails, the dependency graph lights up automatically."

#### Questions This Situation Answers

| Question | How to Answer |
|---|---|
| **Q1** — "How do you build a technical roadmap for an infrastructure platform serving 20+ teams?" | Discovery interviews → strategic alignment → DAG-based sequencing → quarterly milestones with independent value → different communication for different audiences. Key: each milestone must be independently valuable. *Members First angle:* "I sequenced based on what unlocked the most member-facing value earliest." |
| **Q2** — "You have a 2-year infrastructure modernization. How do you structure it?" | 18-month modernization from Java to Rust/CUDA. 5 quarterly milestones, each delivering measurable improvement. Strangler fig pattern: old system ran in parallel. Off-ramps at every milestone. *Trust angle:* "Every milestone had a public go/no-go decision — no one was locked in." |
| **Q5** — "Starting a new infrastructure team. First 90-day plan?" | Days 1-30: Profiled with Nsight (found 150µs launch overhead). Days 31-60: Quick win (HugePages + NUMA → TLB miss 4.2% → 0.3%). Days 61-90: Full roadmap based on profiling evidence. Build credibility by delivering measured improvement first. |
| **Q6** — "How do you create alignment between infrastructure and product roadmaps?" | Joint planning: infra milestones expressed as product capabilities unlocked ("Q2 GPU scoring → real-time fraud model updates hourly instead of daily"). Shared OKR. *One LinkedIn angle:* "I embedded with the consuming team so the roadmap served the whole org, not just infra." |
| **Q7** — "How do you sequence work with dependencies across 5 teams?" | Explicit DAG: Host → GPU → Arrow IPC → LLM → Agent. Named DRI + escalation ladder per edge. Interface contracts early so teams work in parallel. Weekly 15-min sync, blockers only. Critical path buffered 30%. |
| **Q19** — "You're the bottleneck across multiple teams." | Documented interface contracts in shared repo — teams self-served. Created "office hours." Wrote comprehensive spec that killed 80% of questions. Delegated Apple Silicon optimization. *One LinkedIn:* "I turned myself from a serial dependency into a documented contract." |
| **Q32** — "How do you report progress on a multi-quarter project to executives?" | Weekly automated SLO dashboard; monthly one-page readout (milestones, $ impact, decisions needed); quarterly continue/adjust/stop review. Led with outcome, not activity. |
| **Q37** — "Cross-functional dependency blocking you — other team doesn't care." | Apple KG team: understood their priority, showed impact (40% of relevance), shrank the ask (static snapshot vs. pipeline), contributed (loaned engineer). Unblocked in 5 days. *One LinkedIn:* "Make the ask small enough that 'yes' is easier than 'no.'" |

---

### SITUATION 2: Ambiguity / Unclear Requirements / Difficult Target / Ownership

**Theme:** Turn a vague, hard-constrained brief into measurable scope — interview, spike the riskiest assumption, sign off.
**LinkedIn Value:** **Know How** + **Members First**
**Story:** Redesign Siri for HomePod in one quarter, under a 300 ms end-to-end latency budget, with no clear requirements on which use cases to support.

#### Action
- **Interview stakeholders** (Product + BAs) to enumerate probable use cases and define success metrics.
- **Decompose each use case** into required inputs, service calls, and data dependencies.
- **Find the gaps and riskiest assumptions, kick off a SPIKE immediately** (e.g., feasibility of ASR → NLU → retrieval → TTS inside 300 ms).
- **Prioritize use cases** against the latency/timeline constraint and align with stakeholders.
- **TDD with phased delivery** so use cases land incrementally.
- **Sign-off** on scope and success metrics before building.

#### Result
- **Delivered an MVP for the first 2 high-priority use cases** (highest-volume, lowest-latency wins).
- Demonstrated **easier governance, faster iterations, and quicker triage** than the prior process.

#### Impact
- Gave Product a **firm launch date with a clear view of capabilities**.
- Created a **solid foundation for the next one-year plan**.
- The architecture **became a reference for other Apple product lines** (Maps, Music).

#### 🔗 LinkedIn Search/Feed Bridge
> "This maps directly to how I'd approach a Search relevance improvement initiative at LinkedIn. 'Improve search relevance' is just as ambiguous as 'redesign Siri for HomePod.' The approach is identical: (1) Define measurable success — e.g., NDCG@10 improvement, session success rate, zero-result rate reduction. (2) Interview consuming teams — recruiter search, job search, content search have different relevance definitions. (3) SPIKE the riskiest assumption — can we add a semantic re-ranking stage within the p99 latency budget? (4) Ship incrementally — start with the highest-traffic vertical (e.g., people search), prove the lift, then expand. The 300ms budget constraint at Apple is directly analogous to LinkedIn's feed rendering SLA."

#### 🎯 Bar Raiser Deep-Probe Preparation
**Probe:** "How do you know when to stop spiking and start building?"
**Answer:** "I set a time-box and a decision criterion upfront. For the FAISS GPU spike: '2 weeks — if we can prove sub-ms latency at target QPS, we commit. If not, we fall back to BM25-only.' The spike collapsed the uncertainty enough to commit. I don't spike indefinitely — that's analysis paralysis disguised as rigor."

**Probe:** "What if stakeholders can't agree on success metrics?"
**Answer:** "I propose a metric, not wait for consensus. At Capital One, I proposed '15-20% deflection rate' and said 'challenge this in one week or we're locked.' Deadlines force alignment. If two stakeholders have genuinely conflicting metrics, I escalate with options, not a request for a decision."

#### Questions This Situation Answers

| Question | How to Answer |
|---|---|
| **Q21** — "Improve platform reliability — no specific target." | Defined reliability = sub-5ms p99 + 99.999% uptime. Measured current (35ms). Benchmarked against card network SLAs. Gap analysis via Nsight profiling. Proposed targets: sub-10ms Q1, sub-5ms Q2. *Members First:* "I started with the member-facing SLA, not an internal metric." |
| **Q22** — "Build an AI platform with no requirements." | Interviewed 4 teams. Found common patterns. Started with highest-pain capability (agent response time). Built for one use case first, then generalized. 3-month first deliverable with clear success criteria. |
| **Q23** — "New VP wants to modernize everything." | Started with curiosity: "What outcomes are you targeting?" Found alignment. Proposed concrete first step (5% traffic POC). Delivered quick win that built trust. *Trust:* "Be a partner, not a resistor." |
| **Q24** — "Project that three engineers failed to complete." | Apple Siri pipeline — failed due to organizational issues, not technical. Created interface contracts instead of assigning to one team. Set checkpoints with go/no-go criteria. "Same inputs → same outputs; change the approach, not the effort." |
| **Q27** — "When do you have enough information to decide?" | Irreversible ($2.1M GPU) → 4-week POC. Reversible (model size) → decide fast, iterate. Ask: "What information would change my mind?" If POC proved 2.8ms, that's enough for the GPU bet. |
| **Q28** — "Estimate a project you've never done before." | Decompose by uncertainty bucket. Known (2 weeks), partially known (3+1), unknown (4+2). Provide a range (10-16 weeks). SPIKE the biggest unknown early. Actual: 13 weeks, within range. |
| **Q29** — "Technology choice isn't obvious." | Defined weighted criteria (latency 40%, guardrails 25%, ops 20%, familiarity 15%). Prototyped top 2 for 2 weeks. TensorRT-LLM won on non-negotiable criterion. Documented rationale. Made it semi-reversible. |

---

### SITUATION 3: Scope Change / Ownership

**Theme:** Re-scope around the north star — push non-critical work to v2, quantify what's lost vs. gained, run it through CR + sign-off.
**LinkedIn Value:** **Trust** (transparent tradeoffs, no silent scope creep)
**Story:** Mid-program, Product wanted to route every transaction through the Warm Path (Tier-2, 13B reasoning LLM) — not just declines — to capture false negatives and generate training labels. At 24,500 TPS that means running an LLM on 100% of traffic: infeasible on latency and a budget explosion.

#### Action
- **Quantified the impact** in a change request: 13B reasoning on all transactions = N× the GPU fleet = $X/month, and it blows the throughput/latency envelope.
- **Found the real need** — they wanted false-negative _label coverage_, not literally every transaction reasoned.
- **Proposed an alternative**: smart sampling off the hot path — 100% of declines + the uncertainty band (scores near decision threshold) + a small random control sample — run asynchronously.
- **CR process** — documented scope delta and cost of full vs. sampled, presented options A/B/C.
- **Stakeholder sign-off + rebaseline** — agreed on sampling for v1; full coverage gated on v2 ROI review.

#### Result
- Captured **~95% of the labeling value (FN recall) at ~5–10% of the GPU cost**.
- Kept the warm path within SLA and the program within budget; no silent scope creep.

#### Impact
- **Avoided a multi-fold GPU spend** while still improving model recall.
- The **CR + rebaseline discipline** became the default for absorbing scope change.
- Protected the launch timeline.

#### 🔗 LinkedIn Search/Feed Bridge
> "At LinkedIn, scope creep is constant in Feed ranking — 'can we add this new signal to the ranking model?' or 'can we run the re-ranker on all impressions, not just top-k?' The pattern is identical: quantify the compute/latency cost of the new ask, find the real need behind the request (often it's offline evaluation coverage, not online serving), propose a sampling or async alternative, and run it through a change request. LinkedIn's Feed team processes hundreds of ranking experiments per quarter — a disciplined CR process prevents each experiment from becoming permanent scope."

#### 🎯 Bar Raiser Deep-Probe Preparation
**Probe:** "What if the stakeholder insists on full coverage?"
**Answer:** "I'd ask them to co-own the GPU budget increase and the latency SLA relaxation. When the cost is abstract, everyone wants everything. When they have to sign off on $X/month and +Yms latency, they self-prioritize. If they still insist and have the budget authority, I commit — but with monitoring to prove the ROI."

#### Questions This Situation Answers

| Question | How to Answer |
|---|---|
| **Q8** — "Roadmap says 12 months, VP asks for 6." | Apple: didn't say yes or no. Asked what's driving it. Presented 3 options with explicit tradeoffs. Quantified what's lost AND gained. Got explicit agreement on descope. *Trust:* "I was transparent about what we'd sacrifice — no hidden risk." |
| **Q9** — "Roadmap with changing requirements." | Fixed vision, flexible path. Theme-based milestones. Reserved 20% for emergent work. New asks go through CR → sign-off → rebaseline. Architecture designed for capability addition, not feature addition. |
| **Q10** — "Communicate roadmap changes to affected stakeholders." | Communicated proactively in week 2 (not at deadline). Explained WHY, showed TRADEOFF, provided ALTERNATIVE (v2). *Trust:* "Early transparency, even when the news is bad." |
| **Q15** — "Requirements changed at 60%." | Assessed: does work done serve the new requirement? (Yes.) Rebaselined without disrupting in-flight work. Avoided sunk-cost thinking. |
| **Q26** — "Scope creep." | "Yes, and here's the tradeoff: +3 weeks for ~3% lift." Made cost visible. Fixed date, variable scope. Weekly in/out/v2 review with sign-off. Never "no" — always "not now, and here's why." |

---

### SITUATION 4: Conflicts & Stakeholder Management / Disagreements / Escalation

**Theme:** Present options with data, align on decision criteria, keep an escalation ladder ready — resolve at the working level.
**LinkedIn Value:** **One LinkedIn** + **Trust**
**Story:** Disagreement with the Cyber/Security team over using their centralized Guardrail Service for the warm path. Their mandate (PII/PAN filtering before the LLM) was non-negotiable for PCI-DSS, but the service added latency and a failure dependency that broke the warm-path budget.

#### Action
- **Separated the requirement from the mechanism** — compliance was the shared goal; _how_ was the conflict.
- **Measured the cost**: their service added ~X ms/call and a hard dependency threatening the 2–5 s budget at peak.
- **Presented options with data**: (A) Cyber's service as-is; (B) inline Sentinel PII filter certified against Cyber's ruleset; (C) hybrid — inline for hot/warm, central for async cold path.
- **Aligned on decision criteria** — compliance non-negotiable; latency + availability weighted; had Cyber certify the inline filter by running their own test suite.
- **Escalation ladder on standby** but resolved at the working level.

#### Result
- Inline filter **passed Cyber's compliance suite** and stayed within latency budget.
- Conflict resolved **without escalation** — both teams owned the decision.

#### Impact
- Met **PCI-DSS _and_ the latency SLA** — both non-negotiables satisfied.
- Cyber **adopted the certified-inline pattern** for other low-latency services.
- Turned a blocking mandate into a **reusable joint standard**.

#### 🔗 LinkedIn Search/Feed Bridge
> "At LinkedIn, this pattern shows up constantly: the ML platform team mandates a specific model serving framework, but your Search team needs a custom inference path for latency. Or the Trust & Safety team mandates content filtering that adds latency to the Feed. The resolution is always the same: separate the requirement (content safety) from the mechanism (centralized service vs. inline filter), measure the cost, present options, and get the compliance team to certify the alternative. At LinkedIn's scale, these cross-team agreements become platform standards — one resolution benefits hundreds of teams."

#### 🎯 Bar Raiser Deep-Probe Preparation
**Probe:** "What if Cyber refused to certify your inline filter?"
**Answer:** "I'd escalate with data, not emotion. 'Here are two options that both meet compliance. Option B saves X ms per request at Y million QPS. I'm asking for a certification review, not a waiver.' If they still refused, I'd use the escalation ladder — DRI → joint review → VP. But in my experience, showing you've done the work to meet their standard earns cooperation."

**Probe:** "Tell me about a time you were wrong in a stakeholder disagreement."
**Answer:** "VP at Capital One wanted faster phases. I pushed back, but they adjusted the timeline — faster phases with the same safety gates. They were right that I was being too conservative on pace. The phased rollout still succeeded, and I learned that my default caution sometimes needs calibration against business urgency."

#### Questions This Situation Answers

| Question | How to Answer |
|---|---|
| **Q20** — "Manager disagrees with your prioritization?" | VP wanted 100% launch; I ranked phased rollout higher. Understood their perspective. Presented data. Identified root disagreement (risk tolerance, not direction). Made my case, then committed to adjusted timeline. *Trust:* "Disagree transparently, commit fully." |
| **Q30** — "Asked to do something you believe is wrong approach." | ML lead wanted 7B fine-tuning (2 months); I proposed distilled BERT for v1. Proposed 1-week bake-off with pre-agreed decision rule. Data showed 3% delta — not worth schedule risk. Resolve empirically, don't argue. |
| **Q31** — "How do you manage when you'll miss a deadline?" | KV-cache routing: communicated at week 3 (not week 7). Came with diagnosis + options. VP chose "ship now + improve in parallel." *Trust:* "Never surprise a stakeholder." Follow-up: added to estimation checklist. |
| **Q33** — "Stakeholder constantly changes priorities." | 3 changes in 1 quarter. Made cost visible (6 weeks lost). Proposed monthly batched reviews. Found shared architectural foundation. *One LinkedIn:* "Seek one north star that serves the whole org." |
| **Q34** — "How do you say no to a senior leader's pet project?" | Framed against their priority: "Full launch risks repeating the failure that damaged your credibility." Offered alternative: accelerated phases. Never a flat "no" — always "not this way, but here's a better path to your goal." |
| **Q35** — "Build trust with a team burned by platform changes." | (1) Acknowledged past pain. (2) Started small (5% traffic). (3) Weekly transcript reviews. (4) Gave them kill-switch control. (5) Kept every promise. Hostile → champion in 8 weeks. *Care About Each Other:* "They needed to feel in control, not just informed." |
| **Q36** — "Product vs. Engineering disagree." | Root disagreement was risk tolerance, not direction. Created shared data: "at 5% traffic, failure = 500 customers; at 100%, 10,000." Proposed experiment. Empirical resolution, not authority-based. |
| **Q38** — "How do you onboard a new team to your platform?" | Self-service docs, tiered support (docs → Slack → office hours → pairing), golden-path starter kit. Onboarding: 4 weeks → 5 days by third team. *One LinkedIn:* "Platform adoption is a product problem — treat consuming teams as customers." |
| **Q39** — "Competing requests from equally important stakeholders." | Made tradeoff visible to both. Found synergy: model deployment with built-in observability. Sequenced: foundation first, then feature. Both served, neither fully delayed. |
| **Q40** — "Maintaining velocity during leadership transition." | Continued executing. Documented current state. Protected team from uncertainty. Briefed new director: 30-min context dump + "decisions needed from you" list. Zero lost sprints. |

---

### SITUATION 5: Feature vs. Tech Debt

**Theme:** Prioritization by business impact — quantify debt and features in the same currency, then sequence.
**LinkedIn Value:** **Dream Big, Get Things Done**
**Story:** Had to choose between paying down platform tech debt — GPU under-utilization (idle-cost waste) and observability gaps (slow triage) — versus porting a new Identity-Fraud product that carried time-to-market pressure.

#### Action
- **Quantified the debt**: GPU utilization at ~X% = $Y/month wasted; observability gaps = Z-hour mean-time-to-triage per incident.
- **Quantified the feature**: Identity-Fraud product's revenue/strategic value and deadline.
- **Mapped the interaction**: new product would ~2× platform load — onboarding onto an under-utilized, poorly observable base would _amplify_ both debt costs.
- **Sequenced**: fix highest-interest debt the new product _depends on_ first (Helios GPU scheduling + core observability), defer cosmetic debt, then port the product.
- **Made it one visible tradeoff** to leadership: "pay ~3 weeks of debt now to land the product on a stable base, or eat incidents + waste at 2× load."

#### Result
- Landed **GPU scheduling + observability foundation**, then ported Identity Fraud onto a stable base.
- New product onboarded with the platform observable and well-utilized.

#### Impact
- **GPU utilization ~70% → 82%+**, cutting idle cost; **faster triage**.
- Identity-Fraud product shipped with **fewer launch incidents**.
- **Debt-as-dollars** became the shared prioritization language with Product.

#### 🔗 LinkedIn Search/Feed Bridge
> "LinkedIn's Search infrastructure has the same tension: do you pay down the serving framework's tech debt (e.g., migrating from an older Galene index to a new architecture) or ship the next relevance improvement? The answer is the same: quantify both in the same currency — latency, cost, incident rate — and sequence based on dependency. If the new relevance model doubles QPS on a fragile serving layer, you fix the serving layer first. At LinkedIn's scale, the cost of getting this wrong is amplified: a 1% efficiency improvement saves millions; a stability regression during a new model launch affects hundreds of millions of feed impressions."

#### Questions This Situation Answers

| Question | How to Answer |
|---|---|
| **Q3** — "Balance platform work vs. feature requests." | 70/20/10 split. Made platform investment visible via developer-velocity metrics. Showed compounding returns: "20% platform this quarter → ~30% faster delivery next quarter." Never "debt vs. features" — always "sustainable velocity." |
| **Q11** — "30 requests from different teams. How to prioritize?" | Published all requests with impact assessment. Grouped by theme. Applied TIER rubric. Made list + rationale public. 1-week challenge period. *Trust:* "Transparency reduces gaming." |
| **Q12** — "Tech debt vs. features." | Quantified debt in dollars: "Python tax = 15-40ms = $Y/month wasted GPU." Debt on critical path → fix now. High interest rate → schedule. Stable code → leave alone. "Sustainable velocity, not debt vs. features." |
| **Q14** — "Everyone says 'urgent.' How do you handle?" | Visible urgency definition: "Urgent = revenue impact per hour OR SLA breach." Force-ranked publicly. Explicit tiers with response SLAs: P0 (4h), P1 (1 day), P2 (1 sprint), P3 (next quarter). |

---

### SITUATION 6: Bugs & Reliability Incidents / Resource or Budget Constraint

**Theme:** Size impact → contain → RCA → prevent, while fitting the fix inside a fixed GPU budget.
**LinkedIn Value:** **Members First** + **Trust**
**Story:** A cross-tenant leakage incident on the multi-tenant LLM serving layer (one tenant's context surfacing in another's — a sev-1 on a PCI platform), compounded by a resource constraint: no budget for more GPUs.

#### Action
- **Declared incident, sized blast radius**, contained immediately (disabled shared-cache path, isolated traffic); notified compliance same day.
- **RCA** — root cause: shared paged-KV-cache / context reuse across tenant requests without tenant tagging.
- **Fix** — tenant-tagged KV-cache partitioning + request isolation; added cross-tenant guardrail test to CI.
- **Resource constraint** — couldn't buy GPUs, so customized vLLM: per-tenant KV namespaces, paged-attention isolation, FP8 quantization to keep isolation within existing GPU budget.
- **Prevention** — added cross-tenant leakage to chaos/test suite; blameless RCA with action items tracked to closure.

#### Result
- Leak **contained quickly with no confirmed customer data exposure**; passed follow-up audit.
- vLLM customization delivered **tenant isolation within existing GPU budget — zero new spend**.

#### Impact
- Closed a **sev-1 security/compliance risk** and satisfied audit.
- **Saved GPU expansion cost** via quantization/customization.
- **Tenant isolation + CI guardrail became permanent**.

#### 🔗 LinkedIn Search/Feed Bridge
> "LinkedIn runs multi-tenant serving infrastructure at massive scale — the Feed serving layer, the Search index, the Ads ranking pipeline all share compute. Tenant isolation (ensuring one product's traffic spike doesn't degrade another's latency) is a constant concern. The pattern — contain → RCA → prevent with CI guardrails — applies directly. At LinkedIn's scale, the 'no budget for more GPUs' constraint is replaced by 'GPU allocation is centrally managed with quarterly planning cycles,' which creates the same forcing function: you must be creative with existing resources. FP8 quantization, better batching, and smarter cache partitioning are exactly the tools LinkedIn's AI infra team uses."

#### 🎯 Bar Raiser Deep-Probe Preparation
**Probe:** "How do you prevent incidents like this from happening in the first place?"
**Answer:** "Three layers. First, design-time: tenant isolation is a first-class requirement in any multi-tenant system design, not an afterthought. Second, CI-time: cross-tenant tests run on every PR — the guardrail is automated. Third, chaos-time: monthly chaos tests specifically targeting tenant boundaries. The incident taught me that 'shared for efficiency' must always be balanced against 'isolated for safety' — and the default should be isolated unless proven safe."

#### Questions This Situation Answers

| Question | How to Answer |
|---|---|
| **Q4** — "Migration with no downtime." | Dual-read behind feature flag (1% → 10% → 50% → 100%). Automated rollback on precision drop >2%. Offline comparison pipeline. Runbook with decision points. Zero-downtime over 2 weeks. *Members First:* "Zero member-visible impact was the non-negotiable." |
| **Q13** — "P1 security vulnerability." | Non-negotiable top priority. Triaged blast radius, paused lowest-priority work, redirected engineers. Communicated roadmap impact same day. Post-fix: systemic prevention + blameless RCA. |
| **Q16** — "Prioritize reliability with no active incident." | Quantified risk in dollars and incident probability. Used post-mortem trends. Ran chaos test to make latent risk visible to leadership. *Members First:* "Members don't care that we haven't had an incident yet — they care that we won't." |
| **Q17** — "When to stop investing in a failing project." | Broadcom: defined kill criteria BEFORE deployment. When criteria hit, killed immediately — sunk cost irrelevant. Distinguished "not working yet" from "won't work." Made the kill a sign of maturity. |
| **Q18** — "High certainty vs. moonshot." | Portfolio: 70% certainties, 20% de-risked bets (POC before $2.1M commitment), 10% killable exploration. De-risk the uncertain project with minimal investment before full commitment. |
| **Q25** — "Vision but only 2 engineers." | Reduced scope to "prove feasibility." Used managed services. Automated CI early. Showed value in 4 weeks → earned 6 more engineers. *Know How:* "With scarce resources, buy down the biggest risk first." |

---

### SITUATION 7: Mentorship / Prevent Burnout ⭐ NEW

**Theme:** Protect the team — redistribute load, create sustainable velocity, negotiate resources proactively.
**LinkedIn Value:** **Care About Each Other** (primary) + **Trust** + **One LinkedIn**
**Story:** A senior engineer on my team was simultaneously carrying on-call rotation, a multi-week SPIKE (evaluating TensorRT-LLM vs. vLLM for guardrail integration), and ownership of three tech-debt items on the hot path — all at once. The load was unsustainable and I could see the early signs: delayed responses, shorter code reviews, skipped lunch, declining quality on the SPIKE findings.

#### Action

1. **Recognized the pattern early — didn't wait for a breakdown.**
   - I noticed the signals in week 2: SPIKE updates were getting thinner, code review turnaround doubled, and the engineer started working weekends to keep up with on-call + debt work.
   - I initiated a 1:1 conversation. Not "are you okay?" (which gets a reflexive "fine") but: **"I'm looking at your plate right now — on-call, the SPIKE, and three debt items. That's not sustainable. Let's fix this together."**
   - *Care About Each Other:* Named the problem directly without making it about performance.

2. **Immediately relieved the on-call rotation.**
   - Pulled them off on-call for the current rotation cycle and took the next shift myself while I restructured the rotation.
   - Adjusted the on-call rotation to add one more person (me, temporarily) so no one was carrying on-call during a SPIKE again.
   - Created a **policy**: engineers doing active SPIKEs or time-boxed explorations are exempt from on-call for that sprint. This became a team standard.

3. **Split the SPIKE into collaborative work.**
   - The SPIKE was a solo investigation — classic single-point-of-failure pattern. I split it:
     - Engineer kept the TensorRT-LLM evaluation (their deeper expertise).
     - I assigned the vLLM benchmarking to another engineer who'd expressed interest in inference serving.
     - Created a shared comparison template so both halves converged into one decision doc.
   - *One LinkedIn:* Turned a bottleneck into a growth opportunity for a second engineer.

4. **Decomposed tech debt and distributed across the team.**
   - The three debt items were: (a) Python serialization cleanup on Tier 1, (b) stale monitoring scripts, (c) KV-cache eviction policy tuning.
   - I created subtasks for each:
     - (a) Assigned to a mid-level engineer as a stretch task — it required Rust, and they'd been asking for Rust experience. I paired with them for the first day.
     - (b) Assigned to a junior engineer as a well-scoped improvement — clear input/output, good for confidence-building.
     - (c) Kept on the original engineer's plate — it was the most complex and aligned with their SPIKE work.
   - *Care About Each Other:* Redistribution wasn't just load-balancing — it was skill-building for the team.

5. **Negotiated a contract hire from management for short-term relief.**
   - I went to my manager with data: "We have 8 engineers, but on-call + 3 active SPIKEs + quarterly debt budget means we're at 120% capacity this quarter. Next quarter adds the Identity-Fraud onboarding. I need a contract hire for 3 months to cover the gap."
   - Presented it as a **business case**, not a complaint: "A contract hire at $X/month prevents $Y/month in delayed delivery and Z% burnout risk."
   - Got approval within a week. The contractor handled well-scoped integration testing work, freeing the team for higher-judgment tasks.

6. **Established a recurring workload audit.**
   - After this incident, I added a **monthly workload review** to our team rituals: map every engineer's current load across on-call, SPIKEs, feature work, debt work, and review duties.
   - Created a simple heatmap: if any engineer is red (>100% allocation) for two consecutive weeks, it triggers a redistribution conversation.
   - *Trust:* Made workload visible and the redistribution process transparent — no one has to ask for help; the system surfaces it.

#### Result
- Engineer's SPIKE quality **immediately improved** — the TensorRT-LLM evaluation doc became the team's reference for future inference-serving decisions.
- **Zero burnout incidents** in the following two quarters after establishing the workload audit.
- The mid-level engineer who took the Rust serialization task **delivered it independently** and later became the Rust mentor for new team members.
- On-call satisfaction scores (anonymous survey) improved from 3.2/5 to 4.1/5 after the SPIKE-exemption policy.

#### Impact
- **Sustainable velocity**: team maintained consistent sprint velocity instead of the boom-bust pattern of overloaded sprints followed by recovery sprints.
- **Skill distribution**: reduced bus factor — 3 engineers could now work on the hot path instead of 1.
- **Retention signal**: the overloaded engineer later told me in a skip-level that "you stepping in before I had to ask was the reason I stayed." At Staff+ level, **retaining a senior engineer saves 6+ months of hiring and onboarding cost**.
- **Process improvement**: the workload audit and SPIKE-exemption policy were adopted by two adjacent teams.
- *Care About Each Other:* This story demonstrates that caring isn't just empathy — it's **structural**: building systems that prevent burnout rather than reacting to it.

#### 🔗 LinkedIn Search/Feed Bridge
> "At LinkedIn's scale, this is even more critical. Search and Feed teams run complex on-call rotations across multiple services, with constant SPIKEs for new ranking experiments and ongoing tech debt from rapid iteration. The pattern I'd bring: (1) Workload visibility — a shared heatmap so managers and ICs can see load distribution. (2) SPIKE-exemption from on-call — you can't do deep investigation work while being paged. (3) Proactive contract/vendor staffing for well-scoped work during capacity crunches. (4) Skill distribution as a strategic goal — bus factor is a reliability risk, not just a people risk. LinkedIn's 'Care About Each Other' value isn't just about being nice — it's about building teams that can sustain high output over years, not just quarters."

#### 🎯 Bar Raiser Deep-Probe Preparation
**Probe:** "How do you balance protecting one person vs. the team's delivery commitments?"
**Answer:** "They're not in tension — an overloaded engineer produces lower-quality work, creates review bottlenecks, and eventually burns out, which hurts delivery far more than a temporary redistribution. I reframed it for my manager: 'The cost of this engineer burning out is 6 months of lost productivity + hiring. The cost of a contract hire is 3 months at $X. The math is clear.' Protecting one person IS protecting delivery."

**Probe:** "What if the engineer doesn't want help — they see it as a sign of weakness?"
**Answer:** "That's exactly why I didn't frame it as 'you need help.' I framed it as 'the team's load distribution is wrong, and I'm fixing it.' I also shared my own experience: 'I've been in this position — carrying too much and not asking for help is how I learned this lesson the hard way.' Leaders go first in showing vulnerability."

**Probe:** "How do you prevent this from happening again?"
**Answer:** "The workload audit is the structural fix. But the cultural fix is equally important: I normalized the conversation. In sprint planning, I now explicitly ask: 'Who's carrying on-call this sprint? They get 30% less sprint work.' It's not a favor — it's the plan. And when I see someone taking on too much, I intervene in the first week, not the third."

#### Questions This Situation Answers

| Question | How to Answer |
|---|---|
| **Q41** — "How do you ensure your team's wellbeing during high-pressure periods?" | Workload audit, SPIKE-exemption from on-call, proactive redistribution, negotiated contract hire. *Care About Each Other:* "Sustainable velocity over heroic sprints." |
| **Q42** — "How do you maintain team velocity when someone is struggling?" | Recognized signals early, redistributed work as skill-building (not charity), split the SPIKE collaboratively, and backfilled with a contractor. Velocity actually improved because quality went up. |
| **Q19** (alternate angle) — "You're the bottleneck / single point of failure." | The overloaded engineer was a SPOF. Fix: distribute knowledge through paired work on debt tasks, split the SPIKE, and create documentation. Reduced bus factor from 1 to 3 on the hot path. |
| **Q35** (alternate angle) — "Build trust with your own team." | Trust is built by acting before being asked. "You stepping in before I had to ask was the reason I stayed." Leaders demonstrate care through structural changes, not just words. |

---

### Quick Reference: Question → Situation Mapping

| Q# | Question Summary | Situation | LinkedIn Value to Signal |
|---|---|---|---|
| Q1 | Build roadmap for 20+ teams | **1** | Dream Big |
| Q2 | Structure 2-year modernization | **1** | Dream Big |
| Q3 | Balance platform vs. features | **5** | Get Things Done |
| Q4 | Migration with no downtime | **6** | Members First |
| Q5 | First 90-day plan | **1** | Know How |
| Q6 | Align infra + product roadmaps | **1** | One LinkedIn |
| Q7 | Sequence work across 5 teams | **1** | One LinkedIn |
| Q8 | VP compresses timeline | **3** | Trust |
| Q9 | Roadmap with changing requirements | **3** | Trust |
| Q10 | Communicate roadmap changes | **3** | Trust |
| Q11 | Prioritize 30 requests | **5** | Trust |
| Q12 | Tech debt vs. features | **5** | Get Things Done |
| Q13 | P1 security vulnerability | **6** | Members First |
| Q14 | Everyone says "urgent" | **5** | Trust |
| Q15 | Requirements changed at 60% | **3** | Trust |
| Q16 | Prioritize reliability (no incident) | **6** | Members First |
| Q17 | When to stop investing | **6** | Know How |
| Q18 | Certainty vs. moonshot | **6** | Dream Big |
| Q19 | You're the bottleneck | **1** or **7** | One LinkedIn |
| Q20 | Manager disagrees with ranking | **4** | Trust |
| Q21 | Ambiguous "improve reliability" | **2** | Members First |
| Q22 | "Build an AI platform" | **2** | Know How |
| Q23 | New VP wants modernization | **2** | Trust |
| Q24 | Project failed 3 times | **2** | Know How |
| Q25 | Limited resources (2 engineers) | **6** | Know How |
| Q26 | Scope creep | **3** | Trust |
| Q27 | Enough info to decide? | **2** | Know How |
| Q28 | Estimate novel project | **2** | Know How |
| Q29 | Technology choice unclear | **2** | Know How |
| Q30 | Disagree with approach | **4** | Trust |
| Q31 | Will miss deadline | **4** | Trust |
| Q32 | Report to executives | **1** | Trust |
| Q33 | Stakeholder changes priorities | **4** | One LinkedIn |
| Q34 | Say no to senior leader | **4** | Trust |
| Q35 | Trust with burned team | **4** or **7** | Care About Each Other |
| Q36 | Product vs. Engineering disagree | **4** | One LinkedIn |
| Q37 | Cross-functional dependency | **1** | One LinkedIn |
| Q38 | Onboard new team | **4** | One LinkedIn |
| Q39 | Competing equal requests | **4** | One LinkedIn |
| Q40 | Velocity during transition | **4** | Trust |
| Q41 | Team wellbeing under pressure | **7** | Care About Each Other |
| Q42 | Maintain velocity when someone struggles | **7** | Care About Each Other |

---

### LinkedIn Values — Story-to-Value Cross-Reference

This table helps you quickly identify which story demonstrates which value, so you never repeat the same value signal twice in the same interview loop.

| LinkedIn Value | Best Story | Backup Story | Key Phrase |
|---|---|---|---|
| **Members First** | Situation 6 — cross-tenant leakage ("member data safety was non-negotiable") | Situation 2 — Siri HomePod ("I started with the member-facing SLA") | "The member experience drove my decision." |
| **Trust** | Situation 4 — Cyber disagreement ("transparent options, no backroom deals") | Situation 3 — scope change ("communicated early, not at the deadline") | "I communicated at week 3, not week 7." |
| **Care About Each Other** | Situation 7 — burnout prevention ("I intervened before being asked") | Situation 4 — burned Ops team ("they needed to feel in control") | "Sustainable velocity over heroic sprints." |
| **Dream Big, Get Things Done, Know How** | Situation 1 — Capital One rebuild ("35ms → 3.8ms, $45M savings") | Situation 5 — debt vs. features ("quantified both in dollars") | "The vision was ambitious, but I de-risked it with milestones." |
| **One LinkedIn** | Situation 4 — certified-inline pattern reused org-wide | Situation 7 — workload policy adopted by adjacent teams | "I optimized for the whole org, not just my team." |

---

### At LinkedIn's Scale — Prepared Bridges for Every Situation

Keep these in your back pocket. Don't volunteer them — but when the interviewer asks "how would this work at our scale?", you're ready.

| Situation | Your Scale | LinkedIn's Scale | What Changes |
|---|---|---|---|
| **1 — Cross-org** | 5 teams, 24,500 TPS | 100+ teams, millions QPS | Interface contracts enforced by automated contract tests in CI, not just docs. Dependency DAG needs automated health monitoring — can't rely on weekly syncs. |
| **2 — Ambiguity** | 300ms budget, 1 pipeline | Hundreds of ranking pipelines, sub-100ms budgets | Success metrics tied to A/B experiment platform (not just offline eval). SPIKEs run on sampled production traffic, not synthetic benchmarks. |
| **3 — Scope change** | Single product, 1 CR | Hundreds of experiments/quarter | CR process must be lightweight (automated experiment review) but gated (latency/cost budgets enforced). Feature flags at massive scale. |
| **4 — Conflicts** | 2-team disagreement | Platform teams serving 100+ consuming teams | Decisions become platform policies, not one-off agreements. Certified patterns published as "golden paths" with self-service adoption. |
| **5 — Debt vs. features** | ~$20M/yr infra | Billions in infra | 1% efficiency improvement = millions saved. Debt quantification becomes a continuous dashboard, not a quarterly exercise. |
| **6 — Incidents** | Single-tenant isolation | Multi-tenant at massive scale | Blast radius assessment is automated. Incident response is follow-the-sun with dedicated SRE partnership. Chaos testing is continuous, not monthly. |
| **7 — Burnout** | 8-person team | 50+ person org | Workload visibility needs tooling (not just a spreadsheet). On-call fairness algorithms. Rotation health dashboards. Manager training on burnout signals. |

---

### Performance Engineering Depth — Cross-Pollinated Into Execution Stories

LinkedIn's Staff loop includes performance engineering drill-downs. Be ready to drop into technical depth from any execution story.

| Execution Story | Performance Depth You Can Drop Into | Trigger Question |
|---|---|---|
| Situation 1 — Capital One roadmap | "Q1 quick win: Nsight profiling showed 150µs CUDA kernel launch overhead was the bottleneck, not kernel execution. HugePages + NUMA pinning → TLB miss rate 4.2% → 0.3%. CUDA Graphs → launch overhead 150µs → 5µs." | "Walk me through how you profiled and optimized." |
| Situation 2 — Siri HomePod | "FAISS GPU integration: 0.4ms per query, sub-ms in-process. Shared memory arena replaced inter-stage gRPC calls. The 300ms budget breakdown: ASR 80ms, NLU 40ms, retrieval 20ms, re-ranking 60ms, TTS 80ms, overhead 20ms." | "How did you fit everything in 300ms?" |
| Situation 3 — Scope change | "Smart sampling: 100% of declines + uncertainty band (scores 0.4-0.8) + 5% random control. Async pipeline via Kafka consumer with exactly-once semantics. GPU cost: ~5-10% of full-traffic inference." | "How did you design the sampling strategy?" |
| Situation 5 — Debt vs. features | "GPU utilization profiling: SM utilization 18% → 72% via micro-batching (batch size 1 → dynamic batching up to 32). Helios scheduler: per-tier GPU memory partitioning with degrade modes." | "How did you get GPU utilization from 18% to 72%?" |
| Situation 6 — Cross-tenant leakage | "vLLM customization: per-tenant KV namespaces in paged attention. FP8 quantization (E4M3 format) to recover memory headroom for isolation. Measured: 0.2% accuracy degradation, 40% memory savings." | "How did you fit tenant isolation into existing GPU budget?" |

---

### Preparation Plan

| Day | Situation | Practice Focus | LinkedIn Value to Emphasize |
|---|---|---|---|
| 1 | 1 (Cross-org) | Capital One roadmap: PRD/TDD → DAG → DRIs → quarterly milestones → executive readouts. Practice the "at LinkedIn scale" bridge. | Dream Big |
| 2 | 2 (Ambiguity) | Siri HomePod: interview → decompose → SPIKE → prioritize → sign-off. Practice dropping into performance depth (300ms budget breakdown). | Members First |
| 3 | 3 (Scope change) | Warm-path ask: quantify GPU cost → find real need → propose sampling → CR → sign-off. Practice the "stakeholder insists" deep probe. | Trust |
| 4 | 4 (Conflicts) | Cyber guardrail: separate requirement from mechanism → options A/B/C → certify → escalation ladder. Practice "when you were wrong" probe. | One LinkedIn |
| 5 | 5 (Debt vs. features) | GPU/observability debt: quantify both in $ → sequence by dependency → make it one tradeoff. Practice performance depth (18% → 72% GPU). | Get Things Done |
| 6 | 6 (Incidents) | Cross-tenant leakage: contain → RCA → prevent → vLLM customization. Practice "prevent in the first place" probe. | Members First |
| 7 | 7 (Burnout) | Overloaded engineer: recognize → relieve → redistribute → negotiate → systematize. Practice "engineer doesn't want help" probe. | Care About Each Other |
| 8 | Mixed drill | Random Q1–Q42: map to situation in <5 seconds, open with Story, land quantified Impact, weave in LinkedIn value. | All five |

---

### Red Flags to Avoid

| Red Flag | Fix | LinkedIn Signal |
|---|---|---|
| Generic framework without example | Always ground in a specific project story | Know How |
| "It depends" without follow-through | State what it depends ON, then give your real decision | Get Things Done |
| Only one option considered | Show 2-3 options even when the choice was obvious | Know How |
| No metrics | Quantify everything: weeks, %, dollars, TPS | Get Things Done |
| Passive victim of ambiguity | Show how YOU created structure from chaos | Dream Big |
| Perfect outcomes only | Show obstacles, adjustments, and what you'd do differently | Trust |
| Activity instead of judgment | Speak in "I," expose the tradeoff and decision rule | Know How |
| Result and Impact collapsed | Result = immediate change; Impact = durable business effect | Dream Big |
| No "members first" framing | Start at least one answer with member/user impact | Members First |
| No team/people dimension | Include at least one answer about protecting or growing your team | Care About Each Other |
| Stories only at your current scale | Have an "at LinkedIn's scale" bridge ready for every story | Dream Big |
| No failure or learning | Share what you'd do differently; show growth | Trust |

---

### Bar Raiser Readiness — Universal Deep Probes

These probes can come on ANY story. Have answers ready:

| Universal Probe | How to Handle |
|---|---|
| "What would you do differently?" | Always have one honest answer per story. Not "nothing" — that signals lack of reflection. E.g., "I'd invest earlier in automated dependency health checks." |
| "Tell me about a time this approach failed." | Broadcom is your go-to failure story. But also: "The KV-cache routing estimation was off by 3 weeks — I learned to add GPU memory behavior to the SPIKE checklist." |
| "How would you do this at LinkedIn's scale?" | Use the scale bridge table above. Key: acknowledge the delta, name the specific change, and show you've thought about it. |
| "How did you know that was the right decision?" | "I defined the decision criteria upfront and measured against them. For the GPU bet: 'If POC hits 5ms at target TPS, we commit.' It hit 2.8ms. For the kill decision: 'If FP > 0.1% after 1 week, we kill.' FP was climbing. The criteria made the decision, not my gut." |
| "What did your team think?" | "I made the rationale transparent. Two team members challenged — one had valid data, one didn't. I adjusted for the valid challenge and explained why I held firm on the other. Disagreement is healthy; unresolved disagreement is toxic." |
| "How do you handle it when you're wrong?" | "I own it publicly. At Broadcom, I owned the model failure in the post-mortem. At Capital One, when the VP's faster-phase suggestion turned out to be right, I acknowledged it. Being wrong isn't the problem — being wrong and hiding it is." |
