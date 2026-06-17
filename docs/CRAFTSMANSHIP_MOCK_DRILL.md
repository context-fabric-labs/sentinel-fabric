# Craftsmanship Round — 20-Minute Mock Drill (No Answers)

> Pure recognition + rehearsal training. The flow for every question is:
> 1. **INTERVIEWER** (Daniel) fires a real LinkedIn Craftsmanship question — order shuffled across all 6 system areas.
> 2. **COACH** (Karen) names the signal word and the story to reach for.
> 3. **Long silence** — you give the STAR answer out loud yourself. No model answer is played.
>
> The 6 areas: 1 Code Quality · 2 Testing · 3 Mentoring · 4 Trade-offs · 5 Past Projects · 6 Org Scale.
> The stories (reused from Execution): 1 Capital One · 2 Siri HomePod · 3 Scope/sampling · 4 Cyber Guardrail · 5 Feature-vs-Debt · 6 Cross-tenant incident · 7 Burnout · plus Apple observability, Apple KG, Broadcom kill, Fiserv feature store.

---

## INTRO

**COACH:** This is the Craftsmanship mock round. Same game as Leadership — I ask a real question, I name the signal and the story to reach for, then I go quiet and you give the full STAR answer out loud. Craftsmanship is experience-based, so don't just name the story — land the situation, your decision, the result, and a quantified impact. Lead with the engineering judgment, then drop into depth if I'd push. Ready? Here we go.

---

**INTERVIEWER:** You can optimize for only two of scalability, performance, and quality. Which two, and why?
**COACH:** Signal: "pick two of three." Trade-offs — Story 1, the hot path. You chose performance and quality, and bought scalability through tiering. Go.

**INTERVIEWER:** How did you teach a junior engineer about craftsmanship? Give me a specific example.
**COACH:** Signal: "teach a junior." Mentoring — Story 7, the Rust mentee. Apprenticeship on real work, benchmark first. Go.

**INTERVIEWER:** What's the significance of a test suite, and how do you decide what to test and what not to?
**COACH:** Signal: "what to test, what not to." Testing — Story 1 plus the cross-tenant guardrail. Blast radius times change frequency. Go.

**INTERVIEWER:** What's the most technically challenging project you've worked on, and what made it hard?
**COACH:** Signal: "most challenging project." Past Projects — Story 1, Capital One. The hard part was four non-negotiables binding at once. Go.

**INTERVIEWER:** A senior engineer keeps writing code that doesn't meet the team's standards. How do you handle it?
**COACH:** Signal: "senior below standard." Code Quality and conflict — Story 7 plus CI enforcement. One-on-one, then automate the bar. Go.

**INTERVIEWER:** How do you ensure craftsmanship scales beyond a single team, past Dunbar's number?
**COACH:** Signal: "beyond one team." Org Scale — Story 4 golden paths plus Story 7 workload audit. Encode it, don't evangelize it. Go.

**INTERVIEWER:** How do you define quality code? Give me a concrete example.
**COACH:** Signal: "define quality." Code Quality — Story 1, the hot path. Readable, correct at boundaries, operationally predictable. Go.

**INTERVIEWER:** Tight deadline, the PM wants to skip integration tests. How do you negotiate?
**COACH:** Signal: "skip the tests." Trade-offs — Story 3 negotiation. Never a blanket skip — per test, by blast radius. Go.

**INTERVIEWER:** Describe a production incident you owned — the root cause, and the systemic changes you drove.
**COACH:** Signal: "incident you owned." Past Projects — Story 6, the cross-tenant leak. Prevent at a different layer than the fix. Go.

**INTERVIEWER:** How do you test data pipelines differently from microservices? Should data be a first-class artifact?
**COACH:** Signal: "data as an artifact." Testing — Story 3, the warm-path labeling pipeline. Schema, distribution, lineage, reproducibility. Go.

**INTERVIEWER:** How do you decide whether to refactor a working system or rebuild it?
**COACH:** Signal: "refactor versus rebuild." Trade-offs — Story 1, Java to Rust, strangler fig. Rebuild only where the architecture is the constraint. Go.

**INTERVIEWER:** How do you train a senior engineer on craftsmanship?
**COACH:** Signal: "train a senior." Mentoring — Story 4, peer influence. Align to a shared principle, lead with your own mistake. Go.

**INTERVIEWER:** Walk me through how you'd review a PR for a latency-critical service like Feed ranking.
**COACH:** Signal: "review a latency-critical PR." Code Quality — Story 1 hot-path checklist. Blast radius, latency, failure behavior, observability, rollback. Go.

**INTERVIEWER:** Tell me about a time you took on tech debt deliberately. How did you track and repay it?
**COACH:** Signal: "deliberate tech debt." Trade-offs — Story 5, the debt register. Debt-as-dollars, sequenced by dependency. Go.

**INTERVIEWER:** What's your proudest piece of engineering work, and why?
**COACH:** Signal: "proudest work." Past Projects — Story 6, the FP8 tenant isolation. Craft under constraint, zero new spend. Go.

**INTERVIEWER:** How do you establish and evolve coding standards for a new team without stifling creativity?
**COACH:** Signal: "standards without stifling." Mentoring and culture — Story 4, golden paths. Automate the non-negotiable few, free the flexible many. Go.

**INTERVIEWER:** Tell me about a test you wrote, or didn't write, that caught or missed a critical bug.
**COACH:** Signal: "a test that caught or missed." Testing — Story 6, the missing isolation test. A missing test is a silent design assumption. Go.

**INTERVIEWER:** Is there a cost to code review? How do you balance thoroughness against velocity?
**COACH:** Signal: "cost of code review." Code Quality — Story 1 plus Story 5, tiered review. Thoroughness scales with blast radius. Go.

**INTERVIEWER:** How do you reduce friction in producer-consumer relationships between engineering teams?
**COACH:** Signal: "producer-consumer friction." Org Scale — Story 1, interface contracts. Make the contract the product, enforce it in CI. Go.

**INTERVIEWER:** You inherit a service with no tests and you have to add features. What do you do?
**COACH:** Signal: "inherited, no tests." Testing — Story 5, the legacy Java stack. Characterization tests around the seam you're cutting. Go.

**INTERVIEWER:** Tell me about a significant architectural decision that had long-lasting impact.
**COACH:** Signal: "long-lasting architecture decision." Past Projects — Story 1, hot-warm-cold tiering. Judged by what it let you change later. Go.

**INTERVIEWER:** A new hire calls your testing practices overkill. How do you respond?
**COACH:** Signal: "testing is overkill." Mentoring and influence — Story 6 as evidence. Prove the why with a real scar, concede where they're right. Go.

**INTERVIEWER:** How do you balance building internal tools and platforms against feature work?
**COACH:** Signal: "platform versus features." Org Scale — Story 5, seventy-twenty-ten. Justify the platform slice in velocity currency. Go.

---

## OUTRO

**COACH:** That's the full twenty-three. If you reached for the right story before I named it, and you landed a quantified impact each time, you're ready. The Craftsmanship trick is to make every answer concrete — a real decision, a real number, a real lesson — and to be ready to drop into the engineering depth the moment they push. Run it again and beat me to the story every time.
