# The Warlock in PD2's style

The Warlock is the one class not held to the PD2 baseline. It is D2R's Warlock on build 93847, changed only where a
skill falls short of PD2's style: a skill that already meets the bar below stays as D2R has it. No full redesign is
needed. These are its goals, set in [#149](https://github.com/synpoox/pd2-resurrected/issues/149). The skill tree is
drafted in [#150](https://github.com/synpoox/pd2-resurrected/issues/150); balance targets and Warlock items come after it.

## Goals

1. **Identity.** Keep D2R's three trees and their themes: Demon (summons, blood), Eldritch (sword, blades, hexes) and
   Chaos (fire, sigils, miasma).
2. **Skill count.** The class ends at 33 skills, 11 per tree, like PD2's classes. The alpha ships D2R's 30 skills,
   changed where needed, with one grid position per tree left empty; the three new skills fill those positions in the beta.
3. **The PD2-style bar.**
   - Every tree carries at least one full endgame build.
   - Main skills get PD2-style synergies written on their descriptions, mostly from their own tree.
   - No dead skills: each skill is a main skill, a synergy, a working one-point utility or a prerequisite worth taking.
   - A mastery per tree is not required.
4. **Allowed changes.** Numbers, synergies, prerequisites, a skill's position inside its own tree, and reworking how one
   skill works while it keeps its theme. Not allowed: moving a skill to another tree, replacing a skill, re-theming a
   tree or reworking a whole tree; PD2 has never done these.
5. **Engine budget.** Only engine rules the port already has or builds anyway: D2R's Warlock functions and PD2's
   functions ported for the other classes. Each new engine rule needs the owner's approval.
6. **Baseline.** D2R's Warlock on build 93847. The change log below records every change from it and why. A D2R patch
   that changes the Warlock is reviewed as a re-baseline, not followed.

## Changes from D2R's Warlock

| Skill | Change | Why |
|---|---|---|
