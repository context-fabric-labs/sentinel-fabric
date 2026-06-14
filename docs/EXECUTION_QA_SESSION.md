# Execution Round — 15-Minute Random Q&A Voice Session

> One continuous mock interview. The flow for every question is:
> 1. **INTERVIEWER** (Daniel) fires a question — order is shuffled across all 7 stories.
> 2. **COACH** (Karen) pauses you: names the *signal word*, maps it to the right story bucket.
> 3. **YOU** (Samantha) answer in SARI.
>
> Train the reflex: hear question → spot signal → name bucket → tell story → land a number.

---

## INTRO

**COACH:** Alright — this is a fifteen-minute random-fire session. I'll ask, then before you answer I'll point out the signal word and the story bucket it maps to. Listen for the tell in every question. There are only seven stories. Let's go.

---

## ROUND 1

**INTERVIEWER:** We want you to build an AI platform for the org. There are no real requirements yet — just go make it good. Where do you start?

**COACH:** Hear it? "No real requirements." That's the Ambiguity tell. Don't reach for architecture — reach for Story 2, Siri HomePod. Your job is to kill the ambiguity first. Go.

**YOU:** Right — "build an AI platform, no requirements" is the same shape as "redesign Siri for HomePod in a quarter." The danger is it could mean a hundred things. So I treat ambiguity as the first problem to solve. I'd interview the consuming teams to enumerate the probable use cases and define what success means in numbers. Then I decompose each use case into the service calls and data it needs, and I find the single riskiest assumption and spin up a SPIKE on it immediately — time-boxed, with the decision rule written down. Then I prioritize against the hardest constraint, get explicit sign-off on scope and metrics, and build with phased delivery. On Siri that approach got us an MVP for the two highest-volume use cases on a firm date, and the architecture became a reference for Maps and Music. The principle: I create structure from chaos rather than waiting for someone to hand me the spec.

---

## ROUND 2

**INTERVIEWER:** A P1 security vulnerability just landed in your platform. Walk me through it.

**COACH:** The tell is "P1" and "security." That's an Incident question. Story 6 — the cross-tenant leak. The cadence is fixed: contain, root-cause, prevent. Don't jump to the fix before you've sized the blast radius. Go.

**YOU:** My cadence is always contain, then root-cause, then prevent. We had exactly this — a cross-tenant leakage on the multi-tenant LLM serving layer, one tenant's context surfacing in another's. On a PCI platform that's a sev-1. So first I declared the incident and sized the blast radius, contained immediately by disabling the shared-cache path and isolating traffic, and notified compliance the same day. The RCA found the root cause — a shared KV-cache reused across tenants with no tenant tagging. The fix was tenant-tagged cache partitioning plus a cross-tenant guardrail test in CI. And there was a twist — no budget for more GPUs — so I customized vLLM with per-tenant namespaces and FP8 quantization to fit the isolation inside the existing fleet. Result: contained fast, no confirmed data exposure, passed the audit, zero new spend. And the CI guardrail made it permanently impossible to regress.

---

## ROUND 3

**INTERVIEWER:** Your team keeps getting pulled between platform work and product feature requests. How do you balance it?

**COACH:** "Platform versus features" — that's the Debt tell. Story 5. The trap is arguing "debt versus features." Don't. Put both in the same currency. Go.

**YOU:** I never frame it as "debt versus features" — that's a losing framing. I put both in the same currency: dollars and risk. I had this fork at Capital One — platform debt, low GPU utilization burning idle cost and observability gaps slowing triage, versus a new Identity-Fraud product with deadline pressure. I quantified the debt in dollars a month wasted and hours of triage per incident, and the feature in revenue and deadline. The key insight was the interaction: the new product would roughly double platform load, so onboarding it onto a shaky base would amplify the debt costs. So I sequenced — fix the high-interest debt the product actually depends on first, defer the cosmetic stuff, then port. GPU utilization went from about seventy to over eighty-two percent, the product shipped with fewer incidents, and "debt-as-dollars" became the shared language with product.

---

## ROUND 4

**INTERVIEWER:** There's a cross-functional dependency blocking you, and the other team frankly doesn't care about your deadline. What do you do?

**COACH:** "Cross-functional dependency," "other team doesn't care." That's a Dependencies question — Story 1, Capital One. The move isn't escalation first; it's shrink the ask. Go.

**YOU:** My first move is never escalation — it's to make "yes" easier than "no." I had this when the knowledge-graph team slipped on entity embeddings I needed. So I started by understanding their priorities, then I showed them the impact — this fed forty percent of relevance. Then, critically, I shrank the ask: instead of the full live pipeline, I asked for a static snapshot, which was cheap for them. And I contributed — loaned them one of my engineers. We were unblocked in five days. I had an escalation ladder ready — named DRI, then eng-lead sync, then VP at forty-eight hours — but I never needed it because I made the ask small enough that helping me cost them almost nothing.

---

## ROUND 5

**INTERVIEWER:** A senior leader has a pet project you think is the wrong call. How do you say no?

**COACH:** "Say no to a senior leader." That's a Conflict question — Story 4. The rule: never a flat no. Frame it against their own goal. Go.

**YOU:** I never give a flat "no" — I frame it against their own goal. At Capital One a VP wanted a full hundred-percent launch; I believed a phased rollout was right. So instead of "no," I said: "A full launch risks repeating the exact failure that damaged your credibility last time — here's a path that gets you to the same outcome faster and safer." I offered accelerated phases with the same safety gates as the alternative. So it's never "no," it's "not this way — and here's a better route to the thing you actually want." That reframes me as helping them win, not blocking them. And when they pushed the pace and turned out to be right, I adjusted and said so.

---

## ROUND 6

**INTERVIEWER:** How do you protect your team's wellbeing during a high-pressure crunch?

**COACH:** "Wellbeing," "high-pressure." That's the Burnout tell — Story 7. Caring isn't empathy, it's structural. Lead with the early-signal recognition. Go.

**YOU:** For me caring isn't just empathy — it's structural, building systems that prevent burnout instead of reacting to it. I had a senior engineer carrying on-call, a multi-week SPIKE, and three debt items at once. I spotted the early signals in week two — thinner updates, doubled review turnaround, weekend work. So I acted before a breakdown. In the 1:1 I named it directly instead of asking "are you okay." Then I fixed the structure: pulled them off on-call and took the shift myself, made SPIKE-exemption from on-call a team policy, split the SPIKE with another engineer who wanted the growth, and distributed the debt as skill-building. I also took a business case to my manager for a contract hire — framed as "burnout costs six months of hiring, the contractor is three." We had zero burnout incidents the next two quarters, and that engineer told me in a skip-level that me stepping in before they had to ask was why they stayed.

---

## ROUND 7

**INTERVIEWER:** Your roadmap says twelve months. A VP wants it in six. Go.

**COACH:** "Twelve months, wants it in six." Timeline got cut — that's the Scope tell. Story 3. Don't say yes, don't say no. Options with tradeoffs. Go.

**YOU:** I don't say yes and I don't say no — I make the tradeoff visible. First I'd ask what's driving the six months, because the real constraint changes the answer. Then I'd come back with three options, each with what's gained and what's lost: option A, the full scope in twelve; option B, a six-month version that descopes these specific capabilities to v2; option C, six months at higher risk with extra resources. I quantify what we sacrifice in each so they can choose with eyes open, and I get explicit agreement on the descope so there's no hidden risk later. That's the discipline I used at Apple — transparent tradeoffs, then commit fully to whichever path they pick.

---

## ROUND 8

**INTERVIEWER:** You've got work spread across five teams with real dependencies between them. How do you sequence it?

**COACH:** "Five teams," "dependencies," "sequence." Pure Dependencies — Story 1. Talk about the DAG, DRIs, and interface contracts up front. Go.

**YOU:** I model the dependencies as an explicit graph and define the interface contracts up front so teams work in parallel instead of waiting on each other. On the Capital One rebuild the chain was host, then GPU, then the Arrow IPC layer, then the LLM, then the agent. Every edge in that graph had a named DRI and a three-step escalation ladder. I defined the contracts between tiers before anyone built, which let the five teams move in parallel against stable interfaces. I ran a fifteen-minute blockers-only sync, and I buffered the critical path by thirty percent so one slip didn't cascade. The result was that no milestone slipped the critical path the whole year, because the contracts held the teams together.

---

## ROUND 9

**INTERVIEWER:** You're asked to do something technically that you believe is the wrong approach. How do you handle it?

**COACH:** "Wrong approach," a technical disagreement. Conflict — Story 4. The move: don't argue, resolve it empirically with a pre-agreed decision rule. Go.

**YOU:** I resolve technical disagreements empirically rather than by arguing. An ML lead wanted to fine-tune a 7-billion-parameter model, which was about two months of work; I thought a distilled BERT would do for v1. Instead of debating opinions, I proposed a one-week bake-off with the decision rule agreed before we started — whichever hits the accuracy bar within the latency budget wins. The data showed a three-percent delta, not worth the schedule risk, so we went with the smaller model and the lead was on board because the criteria decided it, not my authority. If the data had gone the other way, I'd have happily been wrong. Empirical beats hierarchical every time.

---

## ROUND 10

**INTERVIEWER:** There's no active incident, but you want to invest in reliability. How do you justify it?

**COACH:** "No active incident" but "reliability." Still an Incident-family question — Story 6. The trick: make the latent risk visible before it bites. Go.

**YOU:** The hard part is there's no fire yet, so I have to make the latent risk visible. I quantify it in dollars and incident probability, using post-mortem trends to show the pattern. And the most effective move is to run a chaos test that makes the hidden risk concrete for leadership — instead of saying "we might have an outage," I show "here's the outage, in a controlled test, and here's the member impact." I frame it as Members First: members don't care that we haven't had an incident yet, they care that we won't. That turns an abstract "invest in reliability" into a visible, quantified risk that's easy to prioritize against features.

---

## ROUND 11

**INTERVIEWER:** Classic scope creep — the asks keep growing. How do you manage it?

**COACH:** "Scope creep." That's Story 3 again, but a different angle. Never "no" — always "yes, and here's the cost." Make the price visible. Go.

**YOU:** My answer is never a flat "no" — it's "yes, and here's the tradeoff." When someone adds an ask, I make the cost visible immediately: "Sure — that's plus three weeks for about a three-percent lift; do we want to trade the date for it?" I fix the date and treat scope as the variable. I run a weekly in-out-and-v2 review where every addition is explicitly signed off, parked to v2, or it bumps something out. The discipline is that scope changes go through a quick change request with a rebaseline, so there's never silent creep — the cost is always attached to the ask, and the stakeholder self-prioritizes once they see the price.

---

## ROUND 12

**INTERVIEWER:** You've got a technology choice where the right answer isn't obvious. How do you decide?

**COACH:** "Technology choice, not obvious." That's Ambiguity in a decision costume — Story 2. Weighted criteria, prototype the top two, document. Go.

**YOU:** I make the decision criteria explicit before I evaluate anything, so the choice is defensible and not a gut call. On a serving-framework decision I defined weighted criteria — latency forty percent, guardrail support twenty-five, operational maturity twenty, team familiarity fifteen. Then I prototyped the top two for two weeks each, on real workloads, not synthetic benchmarks. TensorRT-LLM won on the non-negotiable criterion, latency, so the data picked it. I documented the rationale so anyone could challenge it, and I kept the choice semi-reversible behind an abstraction. The principle: define what "best" means in numbers up front, then let a short prototype collapse the uncertainty.

---

## ROUND 13

**INTERVIEWER:** You inherit a team that got burned by a previous platform migration and they don't trust you. How do you win them over?

**COACH:** "Burned team," "don't trust you." Trust-rebuilding — Story 4, with a Care About Each Other flavor. Acts, not words. Start small, give them control. Go.

**YOU:** Trust is rebuilt through actions and control, not promises. I had a team that had been burned by a past platform change, so they were hostile. First I acknowledged the past pain out loud — I didn't pretend it didn't happen. Then I started tiny: five percent of traffic, not a big-bang cutover. I gave them the kill-switch, so they were in control, not just informed — that distinction mattered to them. I did weekly transcript reviews so they saw exactly what was happening, and I kept every single promise, however small. Over about eight weeks they went from hostile to champions. The lesson was that a burned team needs to feel in control before they'll trust again — informing them isn't enough.

---

## OUTRO

**COACH:** That's the loop. Notice what you did every time — you heard the question, caught the signal word, named the story before you spoke, and landed a number at the end. Seven stories, every question is just one of them in a different costume. Run this again and try to name the bucket before I do.
