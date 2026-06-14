# Execution Round — Spoken Walkthrough (Listen, Don't Read)

> A natural interviewer ↔ candidate conversation. Read it aloud or feed it to text-to-speech.
> Every answer follows **SARI** (Situation → Action → Result → Impact) but is spoken like a real person — not a bullet list.

---

## 0. The 5-Second Mental Model (Say this to yourself before every answer)

When a question lands, don't reach for an answer — reach for a **story bucket**. Ask: *"What KIND of hard thing is this?"*

| If the question is about… | Go to Story | One-word trigger |
|---|---|---|
| Coordinating many teams / roadmap / "you're the bottleneck" | **1 — Capital One rebuild** | *Dependencies* |
| Vague brief / no requirements / impossible target / estimate the unknown | **2 — Siri HomePod** | *Ambiguity* |
| The ask grew / timeline got cut / scope creep | **3 — Warm-path-for-everything** | *Re-scope* |
| People disagree / conflict / saying no / earning trust | **4 — Cyber guardrail** | *Options + criteria* |
| Tech debt vs. shiny feature / "everything's urgent" | **5 — Debt vs. Identity-Fraud** | *Same currency* |
| Outage / security bug / "do it with no budget" | **6 — Cross-tenant leak** | *Contain → RCA → prevent* |
| Someone's drowning / burnout / team health | **7 — Overloaded engineer** | *Protect the team* |

**The cadence, every time:** Situation is *one breath*. Action is *the bulk*. Result is *the proof*. Impact is *the mic-drop — and it must have a number.*

---

## SEGMENT 1 — Cross-Org Dependencies (Story: Capital One)

**INTERVIEWER:** So, tell me about the biggest cross-team program you've owned end to end. How did you keep it from falling apart?

**YOU:** Sure. The one I keep coming back to is the year I spent rebuilding Capital One's fraud-detection platform. The setup was — we had a legacy Java stack sitting at 35 milliseconds p99, and we were up against a contractual card-network SLA that we kept brushing against. I needed to get that down under 5 milliseconds, at 24,500 transactions a second, and the work spanned five engineering teams that didn't report to me.

So the first thing I did — before anyone wrote a line of code — was write the PRD and the technical design, and define a three-tier architecture: a hot path in Rust and CUDA for the sub-5-millisecond decisions, a warm path with a 13-billion-parameter model for deeper reasoning, and a cold path for triage. The reason that mattered is it gave every team a stable interface contract to build against in parallel.

Then I ran it like a program. One KPI — sub-5 milliseconds p99. A quarterly roadmap where each tier was independently shippable. And I modeled the cross-team dependencies as an actual graph — every edge had a named DRI and a three-step escalation ladder, and I buffered the critical path by thirty percent so one slip didn't cascade.

The result was that each quarter we shipped something independently valuable — hot path, then warm, then cold — and not one milestone slipped the critical path, because the contracts held.

And the impact — we cut infra cost to about a third, which was roughly twenty million a year, about forty-five million in total savings the first year, and we went from 35 milliseconds to 3.8 at full load.

**INTERVIEWER:** You said five teams stayed aligned. What happened the time they *didn't*?

**YOU:** Yeah, it wasn't perfectly clean. On a related effort the knowledge-graph team slipped on entity embeddings, which I needed. Instead of escalating, I shrank the ask — I took a static snapshot instead of the live pipeline, loaned them one of my engineers, and we were unblocked in five days. The escalation ladder was loaded and ready, but we solved it at the working level. And honestly the flip side happened too — a team once challenged my prioritization and they were *right*, they had new data, so I reprioritized. Holding firm when you're right and adjusting when you're wrong are both judgment.

---

## SEGMENT 2 — Ambiguity (Story: Siri HomePod)

**INTERVIEWER:** Tell me about a time you were handed something really vague — no clear requirements — and had to make it real.

**YOU:** The cleanest example is redesigning Siri for the HomePod. I had one quarter, a hard 300-millisecond end-to-end latency budget, and almost no clarity on *which* use cases we were even supposed to support. So the difficulty wasn't technical depth — it was that "make Siri good on HomePod" could mean a hundred things.

So I treated ambiguity as the first problem to kill. I interviewed product and the business analysts to enumerate the probable use cases and, more importantly, to define what "success" actually meant in numbers. Then I decomposed each use case into the service calls and data dependencies it needed. The riskiest assumption — could we even fit speech recognition, understanding, retrieval, and text-to-speech inside 300 milliseconds — I didn't debate it, I spun up a SPIKE immediately to find out. Then I prioritized use cases against that latency and timeline budget, got explicit sign-off on scope and success metrics, and only *then* built, with phased delivery.

The result was we shipped an MVP for the two highest-priority use cases — the highest-volume, lowest-latency wins — on a firm date.

And the impact was bigger than the launch: it gave product a real launch date with a clear picture of capabilities, it became the foundation for the next year's plan, and the architecture ended up as a reference for other Apple product lines like Maps and Music.

**INTERVIEWER:** How do you know when to *stop* spiking and start building? That can run forever.

**YOU:** I time-box it and I write the decision rule down up front. On a FAISS GPU spike, the rule was: "Two weeks. If we prove sub-millisecond latency at target QPS, we commit. If not, we fall back to BM25-only." The spike exists to collapse uncertainty enough to commit — not to reach certainty. Spiking past that point is just analysis paralysis wearing a lab coat.

---

## SEGMENT 3 — Scope Change (Story: Warm-Path-for-Everything)

**INTERVIEWER:** Give me a time the scope changed mid-flight and you had to push back.

**YOU:** Right in the middle of the Capital One program, product came to me and said: route *every* transaction through the warm path — the 13-billion-parameter reasoning model — not just the declines. Their goal was to catch false negatives and generate training labels. The problem is, at 24,500 transactions a second, running a 13B model on a hundred percent of traffic is infeasible on latency and it's a budget explosion.

So I didn't say no, and I didn't just say yes. I quantified it in a change request — full coverage means N-times the GPU fleet, this many dollars a month, and it blows the latency envelope. Then I dug into the *real* need, and it turned out they didn't actually want every transaction *reasoned* — they wanted label coverage on the false negatives. So I proposed an alternative: smart sampling off the hot path — a hundred percent of declines, plus the uncertainty band where scores sit near the threshold, plus a small random control sample — all run asynchronously. I put options A, B, and C in front of them with the cost of each, got sign-off, and rebaselined.

The result — we captured about ninety-five percent of the labeling value at five to ten percent of the GPU cost, and the warm path stayed inside its SLA.

And the impact: we avoided a multi-fold GPU spend while still improving recall, and that change-request-and-rebaseline discipline became the default way we absorbed scope change — no silent creep.

**INTERVIEWER:** What if the stakeholder just insists on full coverage anyway?

**YOU:** Then I ask them to co-own the GPU budget increase and the latency relaxation. When the cost is abstract, everyone wants everything. The moment someone has to sign their name to "this many dollars a month and plus-Y milliseconds," they self-prioritize. And if they still insist and they hold the budget authority — I commit, but I instrument it so we can prove the ROI either way.

---

## SEGMENT 4 — Conflict & Stakeholders (Story: Cyber Guardrail)

**INTERVIEWER:** Tell me about a serious disagreement with another team. How'd you resolve it?

**YOU:** The Cyber security team and I disagreed over their centralized Guardrail Service for the warm path. Their mandate — PII and PAN filtering before anything hits the LLM — was non-negotiable, it was PCI-DSS. But their service added latency and a hard failure dependency that broke my warm-path budget. So we had a real conflict where both sides were right.

The unlock was separating the *requirement* from the *mechanism*. Compliance was the shared, non-negotiable goal — *how* we achieved it was the actual disagreement. So I measured the cost of their service in milliseconds and as an availability risk, then I put three options on the table with data: A, use their service as-is; B, an inline filter in our service, certified against their exact ruleset; C, a hybrid — inline for the hot and warm paths, central for the async cold path. Then I aligned on the decision criteria — compliance is non-negotiable, latency and availability are weighted — and critically, I had Cyber run *their own* test suite to certify my inline filter. The escalation ladder was on standby but we never needed it.

The result: the inline filter passed Cyber's compliance suite and stayed inside the latency budget, and we resolved it at the working level — both teams owned the call.

And the impact — we met PCI-DSS *and* the latency SLA, both non-negotiables. Cyber then adopted that certified-inline pattern for their other low-latency services. So a blocking mandate turned into a reusable joint standard.

**INTERVIEWER:** And if Cyber had just refused to certify your filter?

**YOU:** I escalate with data, not emotion. "Here are two options that both meet compliance. Option B saves X milliseconds per request at Y million QPS. I'm asking for a certification review, not a waiver." If they still said no, I'd walk the ladder — DRI, then a joint review, then VP. But in my experience, when you've visibly done the work to meet someone's own standard, you earn the cooperation.

**INTERVIEWER:** Tell me about a time you were *wrong* in one of these disagreements.

**YOU:** A VP at Capital One wanted faster rollout phases and I pushed back hard on pace. They adjusted — faster phases, but with the same safety gates — and they were right; I'd been over-conservative. The rollout succeeded, and I took away that my default caution sometimes needs to be calibrated against real business urgency.

---

## SEGMENT 5 — Feature vs. Tech Debt (Story: Debt vs. Identity-Fraud)

**INTERVIEWER:** How do you decide between paying down tech debt and shipping the next feature?

**YOU:** I had exactly that fork. On one side, platform debt — GPU utilization was low, so we were burning idle cost, and we had observability gaps that made triage slow. On the other side, a new Identity-Fraud product with real time-to-market pressure. The trap here is treating it as "debt versus features," which is a losing framing.

So I put both in the *same currency*. The debt: GPU utilization at X percent is this many dollars a month wasted, and the observability gaps cost this many hours of mean-time-to-triage per incident. The feature: the Identity-Fraud product's revenue value and its deadline. Then I mapped the interaction — and this was the key insight — the new product would roughly double platform load. Onboarding it onto an under-utilized, poorly-observable base would *amplify* both debt costs. So I sequenced: fix the highest-interest debt the product actually depends on first — the GPU scheduling and core observability — defer the cosmetic stuff, then port the product. And I framed it to leadership as one visible tradeoff: "pay three weeks of debt now to land this on a stable base, or eat incidents and waste at double the load."

The result — we landed GPU scheduling and the observability foundation, then ported Identity-Fraud onto something stable.

And the impact: GPU utilization went from about seventy percent to over eighty-two, cutting idle cost, triage got faster, the product shipped with fewer launch incidents — and "debt-as-dollars" became the shared prioritization language with product going forward.

**INTERVIEWER:** What about when literally everyone says their request is "urgent"?

**YOU:** I make "urgent" mean something specific and public. Urgent equals revenue impact per hour, or an SLA breach — that's it. Then I force-rank in the open, with explicit tiers and response SLAs: a P0 gets four hours, P1 a day, P2 a sprint, P3 next quarter. Once the definition is visible, the word stops being a negotiating tactic.

---

## SEGMENT 6 — Incidents & Constraints (Story: Cross-Tenant Leak)

**INTERVIEWER:** Walk me through a serious production incident you owned.

**YOU:** We had a cross-tenant leakage incident on the multi-tenant LLM serving layer — one tenant's context surfacing in another tenant's responses. On a PCI platform, that's a sev-1. And it came with a constraint: there was no budget for more GPUs to fix it.

So I declared the incident and sized the blast radius first, then contained immediately — I disabled the shared-cache path and isolated the traffic — and I notified compliance the same day. The root cause from the RCA was that we were reusing a shared paged KV-cache across tenant requests without tenant tagging. The fix was tenant-tagged KV-cache partitioning and request isolation, plus a cross-tenant guardrail test added to CI. And because I couldn't buy GPUs, I customized vLLM — per-tenant KV namespaces, paged-attention isolation, and FP8 quantization to claw back enough memory headroom to fit the isolation inside the *existing* fleet. Then for prevention, I added cross-tenant leakage to the chaos suite and ran a blameless RCA with action items tracked to closure.

The result — the leak was contained fast with no confirmed customer data exposure, and we passed the follow-up audit. And the vLLM customization delivered tenant isolation at zero new spend.

The impact: we closed a sev-1 security and compliance risk, saved the entire GPU expansion cost through quantization, and the tenant isolation plus the CI guardrail became permanent.

**INTERVIEWER:** How do you stop something like that from happening in the first place?

**YOU:** Three layers. Design-time — tenant isolation is a first-class requirement in any multi-tenant design, never an afterthought. CI-time — cross-tenant tests run on every pull request, so the guardrail is automated. And chaos-time — monthly chaos tests aimed specifically at tenant boundaries. The lesson was that "shared for efficiency" always has to be weighed against "isolated for safety," and the default should be isolated until proven safe.

---

## SEGMENT 7 — Burnout & Team Health (Story: Overloaded Engineer)

**INTERVIEWER:** Tell me about a time you protected someone on your team from burning out.

**YOU:** I had a senior engineer carrying three things at once — on-call rotation, a multi-week SPIKE evaluating TensorRT-LLM versus vLLM, and three tech-debt items on the hot path. It was unsustainable, and I could see the early signals in week two: the SPIKE updates were getting thinner, code-review turnaround had doubled, and they'd started working weekends.

So I acted early instead of waiting for a breakdown. In the 1:1 I didn't ask "are you okay" — that just gets a reflexive "fine." I named it: "I'm looking at your plate — on-call, the SPIKE, three debt items. That's not sustainable. Let's fix it together." Then I actually fixed the structure. I pulled them off on-call and took the next shift myself, and I made it a policy that anyone on an active SPIKE is exempt from on-call that sprint. I split the SPIKE — they kept the TensorRT side where they had depth, and I handed the vLLM benchmarking to another engineer who *wanted* inference experience, with a shared template so both halves converged. The three debt items I distributed as skill-building — the Rust serialization task went to a mid-level engineer who'd been asking for Rust, and I paired with them on day one. And I took data to my manager and made a *business case* for a three-month contract hire: "the cost of this engineer burning out is six months of lost productivity plus hiring; the contractor is three months at X — the math is clear." Got approval in a week.

The result — the SPIKE quality immediately recovered; that TensorRT evaluation became the team's reference doc. And after I added a monthly workload audit, we had zero burnout incidents over the next two quarters.

The impact compounds: sustainable velocity instead of the boom-bust pattern, the bus factor on the hot path went from one engineer to three, and the engineer told me in a skip-level that "you stepping in before I had to ask was the reason I stayed." At Staff level, retaining a senior engineer is six-plus months of hiring and onboarding saved. And the workload audit and SPIKE-exemption policy got adopted by two adjacent teams.

**INTERVIEWER:** What if the engineer doesn't *want* help — sees it as weakness?

**YOU:** That's exactly why I never frame it as "you need help." I frame it as "the team's load distribution is wrong and I'm fixing it" — the problem is the system, not the person. And I go first on vulnerability: "I've been here, carrying too much and not asking — that's how I learned this the hard way." Leaders model it before they ask for it.

---

## CLOSE — Rapid-Fire Mixed Drill (Test your mapping speed)

> Interviewer fires a question; you name the story bucket in under five seconds, then answer. Practice the *mapping*, not memorizing.

**INTERVIEWER:** "You're the single bottleneck across multiple teams."
**YOU (map):** *Story 1 — turn myself from a serial dependency into a documented contract: shared interface docs, office hours, a spec that killed eighty percent of the questions.* (Or Story 7 if it's a *person* who's the SPOF.)

**INTERVIEWER:** "Estimate a project you've never done before."
**YOU (map):** *Story 2 — decompose by uncertainty bucket, give a range, SPIKE the biggest unknown first. Actual came in at thirteen weeks, inside my ten-to-sixteen range.*

**INTERVIEWER:** "VP wants twelve months done in six."
**YOU (map):** *Story 3 — don't say yes or no; ask what's driving it, present three options with what's lost and gained, get explicit agreement on the descope.*

**INTERVIEWER:** "Build trust with a team that got burned by a past platform change."
**YOU (map):** *Story 4 — acknowledge the past pain, start at five percent traffic, give them the kill-switch, keep every promise. Hostile to champion in eight weeks.*

**INTERVIEWER:** "When do you stop investing in a failing project?"
**YOU (map):** *Story 6 — define kill criteria before you deploy; when they hit, kill it, sunk cost is irrelevant. The kill is a sign of maturity, not failure.*

**INTERVIEWER:** "Big vision, but only two engineers."
**YOU (map):** *Story 6 — cut scope to "prove feasibility," lean on managed services, show value in four weeks, earn the headcount. Buy down the biggest risk first.*

---

### The one thing to carry into the room

You only have **seven stories**. Every question is just one of seven hard things wearing a different costume. Hear the question → name the bucket → tell the story → **land a number.**
