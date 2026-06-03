# PART 3 — EXECUTION ROUND

## Overview

The Execution round evaluates your ability to:
- Plan and deliver complex, ambiguous projects
- Prioritize ruthlessly with incomplete information
- Manage stakeholders with competing needs
- Drive results through organizational complexity
- Handle scope, timeline, and quality tradeoffs

---

## 40 Execution Questions

### Category 1: Roadmap Planning (Questions 1-10)

**Q1:** "How do you build a technical roadmap for an infrastructure platform serving 20+ teams?"
- **Intent:** Multi-stakeholder planning. Balancing platform evolution with consumer needs.
- **Ideal Framework:**
  1. Discovery (stakeholder interviews, pain point analysis, usage patterns)
  2. Strategic alignment (connect to business outcomes, not just technical goals)
  3. Prioritization (impact × urgency ÷ effort, with strategic weight)
  4. Sequencing (dependencies, risk-first, enable parallel work)
  5. Communication (different artifacts for different audiences)
  6. Iteration (quarterly review and adjustment)

**Q2:** "You have a 2-year infrastructure modernization initiative. How do you structure it?"
- **Intent:** Long-horizon planning. Incremental value delivery.
- **Ideal Framework:**
  - Break into 3-month value-delivering milestones
  - Each milestone must be independently valuable (not just "phase 1 of 8")
  - Define off-ramps: at each milestone, evaluate whether to continue
  - Parallel workstreams for independent components
  - Migration strategy: strangler fig over big-bang
  - Track leading indicators, not just lagging

**Q3:** "How do you balance platform work (long-term) with feature requests (short-term)?"
- **Intent:** Strategic patience. Sustainable velocity.
- **Ideal Framework:**
  - 70/20/10 split (features/platform/innovation) as starting heuristic
  - Make platform work visible by tying it to developer velocity metrics
  - Create "platform tax" budget understood by product leadership
  - Show compounding returns of platform investment
  - Use developer satisfaction surveys as signal

**Q4:** "How do you plan for a system migration that can't have downtime?"
- **Intent:** Operational rigor. Risk management.
- **Ideal Framework:**
  - Dual-write / dual-read during migration
  - Feature flags for traffic routing (1% → 10% → 50% → 100%)
  - Automated rollback triggers based on error rate / latency
  - Data validation pipeline comparing old and new systems
  - Migration runbook with decision points and owners
  - Explicit rollback criteria and timeline

**Q5:** "You're starting a new infrastructure team. What does your first 90-day plan look like?"
- **Intent:** Organizational bootstrapping. Prioritization from zero.
- **Ideal Framework:**
  - Days 1-30: Listen, learn, build relationships. Audit existing systems. Identify top 3 pain points.
  - Days 31-60: Quick wins to build credibility. Propose team charter. Establish operating rhythm.
  - Days 61-90: Deliver first meaningful outcome. Publish roadmap. Establish metrics baseline.
  - Throughout: Build trust with stakeholders by delivering on promises.

**Q6:** "How do you create alignment between infrastructure and product engineering roadmaps?"
- **Intent:** Cross-functional planning. Shared language.
- **Ideal Answer:**
  - Joint planning sessions with product engineering leads
  - Shared OKRs that connect infrastructure capabilities to product outcomes
  - Infrastructure roadmap expressed in terms of product capabilities unlocked
  - Regular "what's coming" briefings in both directions
  - Embedded platform engineers in key product teams

**Q7:** "How do you sequence work when you have dependencies across 5 teams?"
- **Intent:** Coordination at scale. Dependency management.
- **Ideal Answer:**
  - Map dependency graph explicitly (use DAG visualization)
  - Identify critical path and parallelize everything else
  - Create interface contracts early (APIs, data formats) so teams work in parallel
  - Buffer critical path items by 30% for risk
  - Weekly cross-team sync focused only on dependencies and blockers
  - Clear escalation path with SLA for unblocking

**Q8:** "Your roadmap says 12 months. Your VP asks for 6. What do you do?"
- **Intent:** Negotiation. Scope management. Honest communication.
- **Ideal Answer:**
  - Don't immediately say yes or no
  - Ask: "What changed? What's driving the compression?"
  - Present options: "In 6 months, here's what we can deliver vs. 12 months"
  - Quantify risk: "Cutting to 6 months means [specific tradeoffs]"
  - Propose middle ground: "Here's the critical 80% we can do in 8 months"
  - Get explicit agreement on what's descoped

**Q9:** "How do you build a roadmap when requirements are constantly changing?"
- **Intent:** Agility at strategic level. Adaptive planning.
- **Ideal Answer:**
  - Fixed vision, flexible path (2-year direction, 1-quarter detail)
  - Invest in capabilities, not features (capabilities serve multiple futures)
  - Theme-based roadmap (areas of investment) vs. feature-based
  - Reserve 20% capacity for emergent work
  - Monthly re-prioritization checkpoints
  - "Bets" framework: high-confidence bets vs. exploration bets

**Q10:** "How do you communicate roadmap changes to stakeholders who were counting on specific deliverables?"
- **Intent:** Trust maintenance. Transparent communication.
- **Ideal Answer:**
  - Proactive communication (don't wait until the deadline)
  - Explain the WHY (what changed, what new information)
  - Show the tradeoff explicitly (what's gained by the change)
  - Provide alternatives or workarounds for affected stakeholders
  - Maintain a "commitment vs. aspiration" distinction in all roadmap communication

---

### Category 2: Prioritization (Questions 11-20)

**Q11:** "You have 30 requests from different teams. How do you prioritize?"
- **Intent:** Framework thinking. Saying no constructively.
- **Ideal Framework:**
  - **Impact Assessment:** Business value × number of teams affected × urgency
  - **Effort Estimation:** T-shirt sizing (S/M/L/XL) for initial sort
  - **Strategic Alignment:** Does this move toward our architectural north star?
  - **Dependencies:** Does this unblock other high-value work?
  - **Category:** Must-do (SLA/compliance) vs. Should-do vs. Nice-to-do
  - Communicate: Publish prioritized list with rationale. Allow challenges.

**Q12:** "How do you decide between reducing technical debt and building new features?"
- **Intent:** Pragmatic judgment. Not religious about either extreme.
- **Ideal Answer:**
  - Quantify debt: What's the ongoing cost? (incidents, developer hours, velocity drag)
  - Is the debt on the critical path? (debt in stable, unchanging code is tolerable)
  - Interest rate: Is the debt getting worse over time?
  - "Boy scout rule" for incremental debt reduction
  - Reserve specific capacity for targeted debt reduction on highest-interest items
  - Never frame as "debt vs. features" — frame as "sustainable velocity vs. short-term output"

**Q13:** "You discover a P1 security vulnerability. What do you sacrifice from the roadmap?"
- **Intent:** Crisis prioritization. Non-negotiables.
- **Ideal Answer:**
  - Security/compliance: non-negotiable, always top priority
  - Immediately triage: Blast radius? Actively exploited? Workaround available?
  - If critical: Stop lowest-priority in-flight work, redirect resources
  - Communicate immediately: stakeholders need to know roadmap is impacted
  - Post-fix: Add systemic prevention to roadmap (not just patch-and-forget)
  - Never sacrifice security response time for feature delivery

**Q14:** "How do you handle when everyone says their request is urgent?"
- **Intent:** De-escalation. Objective criteria.
- **Ideal Answer:**
  - Establish shared definition of urgency (revenue impact per hour, user impact, SLA breach)
  - Create visible prioritization criteria that anyone can self-assess against
  - Force rank publicly (transparency reduces gaming)
  - "If everything is urgent, nothing is" — create explicit tiers
  - Escalation path for genuine disagreements
  - Regular prioritization reviews where stakeholders see the full picture

**Q15:** "You're 60% through a project and realize the requirements have changed significantly. What do you do?"
- **Intent:** Sunk cost awareness. Pivot judgment.
- **Ideal Answer:**
  - Assess: What's the value of what we've built so far? Salvageable?
  - Compare: Cost to complete original vs. cost to pivot vs. cost to restart
  - Avoid sunk cost fallacy (60% complete is irrelevant if it's the wrong thing)
  - Communicate options to stakeholders with honest assessment
  - If pivoting: What's the smallest increment that delivers value on the new requirements?
  - Document the learning — why did requirements change? Preventable?

**Q16:** "How do you prioritize reliability improvements when there's no incident happening?"
- **Intent:** Proactive vs. reactive. Selling prevention.
- **Ideal Answer:**
  - Quantify risk: Probability × impact of potential failures
  - Show historical pattern: "Our last 3 incidents all relate to this class of issue"
  - Frame as insurance: "This reduces P1 risk by X%"
  - Use SLO burn rate as objective signal
  - Game days / chaos engineering to make latent risk visible
  - Connect to cost: "Each P1 costs us $X in engineering time and $Y in business impact"

**Q17:** "How do you decide when to stop investing in a project that isn't delivering expected results?"
- **Intent:** Kill decisions. Intellectual honesty.
- **Ideal Answer:**
  - Define success criteria and kill criteria BEFORE starting
  - Establish checkpoints (time-boxed experiments)
  - Distinguish "not working yet" from "won't work" — leading vs. lagging indicators
  - Sunk cost is NOT a reason to continue
  - Switching cost IS a valid consideration
  - Make kill decisions a sign of good judgment, not failure
  - Celebrate learning from stopped projects

**Q18:** "You have two projects: one with high certainty of moderate value, one with low certainty of enormous value. How do you prioritize?"
- **Intent:** Portfolio thinking. Risk management.
- **Ideal Answer:**
  - Expected value calculation (probability × outcome)
  - But: don't only optimize for expected value — consider variance
  - Can you de-risk the uncertain project with a small investment first?
  - Portfolio approach: mostly certainty, small allocation to high-variance bets
  - Reversibility: if the bet fails, what's the cost?
  - Information value: what do you learn by trying?

**Q19:** "How do you prioritize when you're the bottleneck across multiple teams?"
- **Intent:** Leverage. Scalability of self.
- **Ideal Answer:**
  - First: eliminate yourself as bottleneck (documentation, delegation, self-service)
  - Force-rank by blast radius of NOT helping
  - Invest time in enabling over doing (teach to fish)
  - Create "office hours" vs. "deep work" to batch requests
  - Identify patterns in requests — solve the class, not the instance
  - Be explicit about response SLAs for different urgency levels

**Q20:** "How do you handle prioritization when your manager disagrees with your ranking?"
- **Intent:** Managing up. Professional disagreement.
- **Ideal Answer:**
  - Seek to understand their perspective first (what information do they have that I don't?)
  - Present your reasoning transparently with supporting data
  - Identify the root of disagreement (different facts? different values? different constraints?)
  - If genuinely disagree: make your case, then disagree and commit
  - Document the decision and revisit at agreed checkpoint
  - Build trust through accumulated good judgment over time

---

### Category 3: Ambiguous Projects (Questions 21-30)

**Q21:** "You're asked to 'improve platform reliability.' There's no specific target. How do you proceed?"
- **Intent:** Structuring ambiguity. Creating clarity.
- **Ideal Framework:**
  1. Define "reliability" — what does it mean for this platform? (availability? durability? consistency?)
  2. Measure current state — establish baseline (current SLOs, incident rate, MTTR)
  3. Benchmark — what should we aspire to? (industry standards, business requirements)
  4. Gap analysis — where are we falling short?
  5. Prioritize — which gaps have highest business impact?
  6. Propose targets — specific, measurable, time-bound
  7. Get alignment — confirm with stakeholders
  8. Execute — focused improvements against gaps

**Q22:** "You're told to 'build an AI platform' with no further requirements. What's your approach?"
- **Intent:** Scoping. Stakeholder discovery. Avoiding boil-the-ocean.
- **Ideal Answer:**
  - Interview 10+ potential consumers: What are you building? What's painful today?
  - Identify common patterns (inference serving, feature management, experiment tracking)
  - Start with the highest-pain, most-common capability
  - Build for one real use case first, then generalize
  - Avoid building in vacuum — embed with first customer team
  - 3-month time-boxed first deliverable with clear success criteria

**Q23:** "A new VP joins and wants to 'modernize everything.' How do you partner with them?"
- **Intent:** Navigating new leadership. Constructive influence. Not being a blocker.
- **Ideal Answer:**
  - Start with curiosity: "Help me understand your vision. What outcomes are you targeting?"
  - Provide context they lack (history, why things are the way they are, what's been tried)
  - Find alignment between their vision and existing trajectory
  - Propose concrete first steps (not a 3-year plan)
  - Identify low-hanging fruit that builds trust and momentum
  - Be a partner, not a resistor — but also be honest about constraints

**Q24:** "You're handed a project that three previous engineers have failed to complete. What do you do?"
- **Intent:** Diagnosis. Not assuming you're smarter.
- **Ideal Answer:**
  - First: understand WHY it failed before (talk to the previous engineers)
  - Is it a technical problem or an organizational problem? (often the latter)
  - Is the problem definition correct? (often the problem is mis-scoped)
  - Are the constraints achievable? (sometimes the project should be killed, not re-attempted)
  - If proceeding: Change ONE fundamental assumption/approach (same inputs → same outputs)
  - Set explicit checkpoints for go/no-go decisions

**Q25:** "You have a vision for a system but limited resources (2 engineers). How do you proceed?"
- **Intent:** Minimal viable approach. Leverage. Creativity with constraints.
- **Ideal Answer:**
  - Reduce scope to absolute core (what's the smallest thing that proves the concept?)
  - Build on existing platforms and tools (don't reinvent)
  - Automate ruthlessly (2 people can't manually operate anything)
  - Seek force multipliers (managed services, open source, internal shared platforms)
  - Timebox: show value in 6 weeks to earn more investment
  - Accept higher-than-ideal operational overhead temporarily to ship faster

**Q26:** "How do you scope a project when stakeholders keep adding requirements?"
- **Intent:** Scope management. Constructive pushback.
- **Ideal Answer:**
  - "Yes, and here's the tradeoff" — never just "no"
  - Make cost of each addition visible (timeline impact, quality impact)
  - Fixed date with variable scope, or fixed scope with variable date — pick one
  - MoSCoW prioritization with stakeholders (Must/Should/Could/Won't)
  - Create a parking lot for v2 — legitimize deferral
  - Regular scope reviews with explicit "in/out" decisions documented

**Q27:** "How do you know when you have enough information to make a decision vs. when to gather more?"
- **Intent:** Decision speed vs. quality tradeoff.
- **Ideal Answer:**
  - Irreversibility is the key factor: reversible decisions → decide fast, irreversible → gather more
  - 70% confidence threshold for most decisions (Jeff Bezos principle)
  - Cost of delay: what's the penalty for waiting?
  - Can you make the decision smaller? (reduce blast radius, then decide fast)
  - "What information would change my mind?" — if you can't name it, you have enough
  - Time-box research: "If I don't have clarity by Friday, I decide with what I have"

**Q28:** "You're asked to estimate a project you've never done before. How do you approach it?"
- **Intent:** Estimation honesty. Risk communication.
- **Ideal Answer:**
  - Decompose into known and unknown components
  - Reference class forecasting: "Projects of similar scope have taken X"
  - Provide ranges, not points (best case / likely case / worst case)
  - Identify specific unknowns that create the range
  - Propose a 2-week spike to reduce uncertainty for the biggest unknowns
  - Add explicit contingency for unknowns (1.5-2x for novel work)
  - Track estimates vs. actuals to calibrate over time

**Q29:** "How do you handle a project where the technology choice isn't obvious?"
- **Intent:** Technology evaluation rigor.
- **Ideal Answer:**
  - Define evaluation criteria weighted by project requirements
  - Build a decision matrix (not just gut feel)
  - Prototype the riskiest assumption with top 2 candidates
  - Consider: performance, operational burden, team familiarity, ecosystem, longevity
  - Make the decision reversible if possible (abstraction layers)
  - Document decision rationale for future engineers
  - Time-box the evaluation (don't analysis-paralyze)

**Q30:** "You're asked to do something you believe is the wrong approach. What do you do?"
- **Intent:** Professional courage. Disagree and commit.
- **Ideal Answer:**
  - Articulate concerns clearly with evidence (not just "I don't like it")
  - Understand their reasoning (seek to understand before being understood)
  - If still disagree after understanding: escalate through appropriate channels
  - If decision stands: commit fully to making it succeed (no sandbagging)
  - Document your concerns and the decision for future reference
  - Don't be the "I told you so" person — help course-correct if issues emerge

---

### Category 4: Stakeholder Management (Questions 31-40)

**Q31:** "How do you manage expectations when you know you'll miss a deadline?"
- **Intent:** Trust. Transparency. Proactive communication.
- **Ideal Answer:**
  - Communicate EARLY (as soon as you know, not at the deadline)
  - Come with diagnosis: why, and what you're doing about it
  - Present options: reduced scope on time, full scope delayed, or additional resources
  - Never surprise stakeholders with a miss
  - Propose revised plan that you're confident in (don't be late twice)
  - Follow up: what systemic change prevents this in the future?

**Q32:** "How do you report progress on a multi-quarter project to executives?"
- **Intent:** Executive communication. Signal over noise.
- **Ideal Answer:**
  - Lead with outcome/impact, not activities
  - Traffic light status (Green/Yellow/Red) with clear definitions
  - Highlight decisions needed from them (don't just inform)
  - Risks with mitigations (not just risk list)
  - Milestones achieved and upcoming (not % complete)
  - Keep to one page / 5 minutes
  - Separate the "need to know" from the "nice to know"

**Q33:** "How do you handle a stakeholder who constantly changes priorities?"
- **Intent:** Boundary setting. Managing up.
- **Ideal Answer:**
  - Make the cost of context-switching visible (quantify wasted work, team whiplash)
  - Ask: "What's changed in the business that's driving this?"
  - Propose a structure: "Let's batch priority changes to bi-weekly reviews"
  - Show the cumulative impact of changes (burndown chart of churn)
  - Seek alignment on ONE north star metric that doesn't change
  - If truly dynamic environment: build for flexibility (shorter iterations, modular architecture)

**Q34:** "How do you say no to a senior leader's pet project?"
- **Intent:** Political courage. Constructive pushback.
- **Ideal Answer:**
  - Never just "no" — always "not this, because here's what we'd lose"
  - Frame against their priorities: "This would delay X which you identified as critical"
  - Provide alternatives: "Here's a lower-cost way to test the hypothesis"
  - Offer to help them find resources elsewhere if appropriate
  - If overruled: commit and execute, but ensure the tradeoff is documented
  - Build political capital by delivering on their true priorities

**Q35:** "How do you build trust with a team that's been burned by platform changes before?"
- **Intent:** Trust recovery. Empathy. Delivery credibility.
- **Ideal Answer:**
  - Acknowledge their experience (don't dismiss past pain)
  - Start small: deliver one small thing reliably
  - Over-communicate: status, changes, timeline adjustments
  - Give them control: opt-in migration, feature flags, rollback capabilities
  - Embed with them: office hours, shared Slack channel, pair programming
  - Keep every promise, no matter how small

**Q36:** "How do you handle a situation where Product and Engineering leadership disagree on priorities?"
- **Intent:** Cross-functional alignment. Influence across orgs.
- **Ideal Answer:**
  - Get both parties' underlying goals (positions vs. interests)
  - Often the disagreement is about sequencing, not direction
  - Create shared data: what does the customer data say?
  - Propose experiments that resolve the disagreement empirically
  - If deadlocked: facilitate a decision framework they both agree to BEFORE revealing the answer
  - Escalate to shared boss only as last resort

**Q37:** "You have a cross-functional dependency that's blocking you. The other team doesn't consider it urgent. What do you do?"
- **Intent:** Unblocking. Influence. Creativity.
- **Ideal Answer:**
  - Understand their priorities (why isn't this urgent for them?)
  - Show business impact of the delay (escalate the pain, not the relationship)
  - Can you reduce what you need? (smaller ask that's easier to prioritize)
  - Can you contribute (loan an engineer, do the work yourself)?
  - Create incentive alignment (what do they get if they help you?)
  - If all else fails: can you architect around the dependency?
  - Escalation is last resort, and only after exhausting alternatives

**Q38:** "How do you onboard a new team to your platform without disrupting your existing roadmap?"
- **Intent:** Scaling. Self-service. Sustainability.
- **Ideal Answer:**
  - Self-service onboarding (documentation, tutorials, templates)
  - Tiered support: self-service → community → office hours → dedicated support
  - Dedicated onboarding capacity (budget 10-15% of team for support)
  - First-class onboarding experience (golden path, starter kit)
  - Measure onboarding time and success rate
  - Community-driven support (past adopters help new adopters)
  - DON'T: provide white-glove service that doesn't scale

**Q39:** "How do you handle competing requests from equally important stakeholders?"
- **Intent:** Fairness. Transparency. Decision framework.
- **Ideal Answer:**
  - Make the tradeoff visible to both parties simultaneously
  - Use objective criteria (business impact, alignment, timing)
  - Can you sequence rather than choose? (one now, one next quarter)
  - Can you find synergies? (a solution that partially serves both)
  - Escalate the DECISION (not the relationship) if needed
  - Whatever you decide, explain the rationale transparently to both

**Q40:** "How do you maintain velocity during a leadership transition?"
- **Intent:** Organizational resilience. Autonomy.
- **Ideal Answer:**
  - Continue executing against established strategy (don't pause)
  - Document current state, in-flight work, and rationale clearly
  - Maintain team morale (stability from within)
  - Brief new leadership efficiently (context without bias)
  - Be flexible on direction changes (new leader may have different priorities)
  - Protect the team from organizational thrash
  - Use the transition as an opportunity to reinforce what's working

---

## Ideal Answer Frameworks

### Framework 1: The SCOPE Framework (for Project Questions)

```
S - Situation: What's the context and constraints?
C - Criteria: What does success look like? How do we measure?
O - Options: What are the 2-3 approaches?
P - Plan: What's the recommended approach and why?
E - Execution: How do we deliver? Milestones, risks, dependencies.
```

### Framework 2: The INVEST Framework (for Prioritization Questions)

```
I - Impact: What's the business value?
N - Necessity: Is this a must-have or nice-to-have?
V - Velocity: Does this accelerate future work?
E - Effort: What's the cost?
S - Strategic: Does this align with our direction?
T - Time: Is there urgency or deadline?
```

### Framework 3: The ALIGN Framework (for Stakeholder Questions)

```
A - Acknowledge: Validate their perspective
L - Listen: Understand underlying goals
I - Inform: Share context they may not have
G - Generate: Create options together
N - Navigate: Find mutual path forward
```

### Framework 4: The DECIDE Framework (for Ambiguity Questions)

```
D - Define: What decision are we actually making?
E - Evaluate: What are the criteria?
C - Collect: What information do we need? (and what's "good enough"?)
I - Identify: What are the options?
D - Decide: Choose and commit
E - Execute: Act and measure
```
