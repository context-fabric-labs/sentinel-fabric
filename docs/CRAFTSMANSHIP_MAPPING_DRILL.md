# Craftsmanship Round — 15-Minute Mapping Drill (No Answers)

> Pure recognition training. The flow for every question is:
> 1. **INTERVIEWER** (Daniel) fires a real LinkedIn Craftsmanship question — order shuffled across all 6 areas.
> 2. **COACH** (Karen) names the signal word, the SYSTEM AREA, the story to reach for, and a one-line hint.
> 3. **Silence** — you say out loud which area + story + your one-line approach. No model answer is played.
>
> The 6 areas: 1 Code Quality · 2 Testing · 3 Mentoring · 4 Trade-offs · 5 Past Projects · 6 Org Scale.
> The stories: 1 Capital One · 2 Siri · 3 Sampling pipeline · 4 Cyber Guardrail · 5 Feature-vs-Debt · 6 Cross-tenant · 7 Rust mentee.

---

## INTRO

**COACH:** This is the Craftsmanship mapping drill. The game is fast recognition — I ask, then I name the signal, the system area, and the story to reach for, plus a one-line hint. Then I go quiet. You say out loud: which area, which story, and your one-sentence approach. Don't give the full answer here — just prove you can place the question instantly. Ready? Let's go.

---

**INTERVIEWER:** How do you define quality code? Give me a concrete example.
**COACH:** Signal: "define quality." Area one, Code Quality. Story 1, the hot path. Hint: the next engineer can change it safely at 2am. Go.

**INTERVIEWER:** You can optimize for only two of scalability, performance, and quality. Which two?
**COACH:** Signal: "pick two of three." Area four, Trade-offs. Story 1. Hint: you chose performance and quality, and bought scalability through tiering. Go.

**INTERVIEWER:** How did you teach a junior engineer about craftsmanship?
**COACH:** Signal: "teach a junior." Area three, Mentoring. Story 7, the Rust mentee. Hint: apprenticeship on real work, benchmark first. Go.

**INTERVIEWER:** What's the significance of a test suite, and how do you decide what to test?
**COACH:** Signal: "what to test, what not to." Area two, Testing. Story 1 plus the guardrail. Hint: blast radius times change frequency. Go.

**INTERVIEWER:** A senior engineer keeps writing code below the team's standard. How do you handle it?
**COACH:** Signal: "senior below standard." Area one, Code Quality. Story 7 plus CI. Hint: one-on-one, then encode the bar into CI. Go.

**INTERVIEWER:** What's the most technically challenging project you've worked on?
**COACH:** Signal: "most challenging project." Area five, Past Projects. Story 1, Capital One. Hint: four non-negotiables binding at once. Go.

**INTERVIEWER:** How do you ensure craftsmanship scales beyond a single team?
**COACH:** Signal: "beyond one team." Area six, Org Scale. Story 4 plus Story 7. Hint: encode it, don't evangelize it. Go.

**INTERVIEWER:** Tight deadline — the PM wants to skip integration tests. How do you negotiate?
**COACH:** Signal: "skip the tests." Area four, Trade-offs. Story 3. Hint: never a blanket skip — per test, by blast radius. Go.

**INTERVIEWER:** Describe a production incident you owned.
**COACH:** Signal: "incident you owned." Area five, Past Projects. Story 6, the cross-tenant leak. Hint: prevent at a different layer than the fix. Go.

**INTERVIEWER:** How do you test data pipelines differently from services? Is data a first-class artifact?
**COACH:** Signal: "data as an artifact." Area two, Testing. Story 3, the sampling pipeline. Hint: schema, distribution, lineage, reproducibility. Go.

**INTERVIEWER:** How do you decide whether to refactor a working system or rebuild it?
**COACH:** Signal: "refactor versus rebuild." Area four, Trade-offs. Story 1, Java to Rust. Hint: rebuild only where the architecture is the constraint. Go.

**INTERVIEWER:** How do you train a senior engineer on craftsmanship?
**COACH:** Signal: "train a senior." Area three, Mentoring. Story 4. Hint: align to a shared principle, lead with your own mistake. Go.

**INTERVIEWER:** Walk me through reviewing a PR for a latency-critical service like Feed ranking.
**COACH:** Signal: "review a latency-critical PR." Area one, Code Quality. Story 1 checklist. Hint: blast radius, latency, failure behavior, observability, rollback. Go.

**INTERVIEWER:** Tell me about a time you took on tech debt deliberately.
**COACH:** Signal: "deliberate tech debt." Area four, Trade-offs. Story 5, the debt register. Hint: debt-as-dollars, sequenced by dependency. Go.

**INTERVIEWER:** What's your proudest piece of engineering work?
**COACH:** Signal: "proudest work." Area five, Past Projects. Story 6, the FP8 isolation. Hint: craft under constraint, zero new spend. Go.

**INTERVIEWER:** How do you set coding standards for a new team without stifling creativity?
**COACH:** Signal: "standards without stifling." Area three, Mentoring. Story 4, golden paths. Hint: automate the non-negotiable few, free the flexible many. Go.

**INTERVIEWER:** Tell me about a test that caught — or missed — a critical bug.
**COACH:** Signal: "a test that caught or missed." Area two, Testing. Story 6. Hint: a missing test is a silent design assumption. Go.

**INTERVIEWER:** Is there a cost to code review? Thoroughness versus velocity?
**COACH:** Signal: "cost of code review." Area one, Code Quality. Story 1 plus Story 5. Hint: thoroughness scales with blast radius. Go.

**INTERVIEWER:** How do you reduce friction between producer and consumer teams?
**COACH:** Signal: "producer-consumer friction." Area six, Org Scale. Story 1, interface contracts. Hint: make the contract the product, enforce it in CI. Go.

**INTERVIEWER:** You inherit a service with no tests and must add features. What do you do?
**COACH:** Signal: "inherited, no tests." Area two, Testing. Story 5, legacy Java. Hint: characterization tests around the seam you're cutting. Go.

**INTERVIEWER:** Tell me about an architectural decision with long-lasting impact.
**COACH:** Signal: "long-lasting architecture." Area five, Past Projects. Story 1, hot-warm-cold. Hint: judged by what it let you change later. Go.

**INTERVIEWER:** A new hire calls your testing practices overkill. How do you respond?
**COACH:** Signal: "testing is overkill." Area three, Mentoring. Story 6 as evidence. Hint: prove the why with a real scar, concede where they're right. Go.

**INTERVIEWER:** How do you balance internal tools and platform work against features?
**COACH:** Signal: "platform versus features." Area six, Org Scale. Story 5, seventy-twenty-ten. Hint: justify the platform slice in velocity currency. Go.

---

## OUTRO

**COACH:** That's all twenty-three. If you placed each one — the area, the story, and a one-line approach — before I finished naming it, your reflexes are ready. Run it again until the area and the story arrive the instant you hear the signal. Then move to the full mock round and say the whole answer out loud.
