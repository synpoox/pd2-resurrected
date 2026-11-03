# PD2 Resurrected

A port of Project Diablo II (PD2) onto Diablo II: Resurrected (D2R) as a mod.

## Language

**Game mode**:
The rule set a D2R character is created in: Classic, Lord of Destruction (LoD) or Reign of the Warlock (RotW). The port targets RotW only.
_Avoid_: expansion, version, ladder

**Table bank**:
One of D2R's two folders of data tables; each game mode reads only its own bank.
_Avoid_: excel folder, data set

**D2R-only content**:
Anything D2R has that PD2 does not (items, runewords, areas, features), other than the Warlock. On unless turned off by approval one by one.
_Avoid_: new content, vanilla extras

**Warlock**:
D2R's RotW class, the one class in the port that is designed new in PD2's style rather than ported 1:1.

**Unported list**:
The record of every deviation, its status and why.
_Avoid_: cut list, TODO list

**PD2 baseline**:
The one frozen PD2 build the port is measured against. Moving to a newer PD2 is a deliberate re-baseline.
_Avoid_: latest PD2, current season

**Exact**:
A parity tier: the player observes the same values as in the PD2 baseline (stats, rolls, rates, formulas, names). Identifiers and file layout may differ.
_Avoid_: 1:1, identical, byte-for-byte

**Function-equivalent**:
A parity tier: the player has the same capability as in PD2, in D2R's look and controls.

**Deviation**:
Any observable difference from the PD2 baseline. Every deviation is a row on the unported list, marked approximated, cut or not yet.
_Avoid_: bug, known issue

**Approximated**:
A deviation where the port reproduces a PD2 rule only roughly and states how it differs.

**Cut**:
A deviation where the port does not reproduce a PD2 rule at all.

**Not yet**:
A deviation the port intends to close but has not.

**Engine rule**:
A PD2 behaviour that is not a table value and needs plugin code, such as a skill function, hit function, cube op, item rule or server patch. A skill's server and client halves are one engine rule.
_Avoid_: patch, function number (a function number is where one engine rule plugs in)

**Companion plugin**:
A third-party D2RLoader plugin bundled with the port that stands in for a PD2 feature; what it lacks is a deviation.
_Avoid_: dependency, add-on, external mod
