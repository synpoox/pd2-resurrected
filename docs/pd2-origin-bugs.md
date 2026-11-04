# PD2-origin bugs

Bugs that exist in PD2 itself (its tables, strings or `ProjectDiablo.dll`) and that we fixed in the port instead of
copying them. Each fix is a deliberate deviation from the PD2 baseline, so it is listed here rather than in
[unported.md](unported.md).

A bug belongs here only when it shows on live PD2 too. A bug our port brought in (a D2R string that doesn't fit a PD2
column, say) is an ordinary `bug`.

## How to record one

1. Label the issue `pd2-origin` (with `bug`). Say in the body how it shows on live PD2.
2. The PR that fixes it links the issue as usual (`Closes #<n>`).
3. Add a row below in that PR.

List them with `gh issue list --label pd2-origin --state all`.

| Bug | What PD2 does | Our fix | Issue | PR |
|---|---|---|---|---|
| Blade Fury fragment direction | A fragment's direction is a signed `% 64` (`0x103a7c70`, `0x103a6530`): when the offset takes it below direction 0 the index is negative and reads before 1.13c's 64-direction tables (the Y table's tail as x, the bytes before it as y), so the fragment flies along a short, wrong vector, roughly west where it should leave near east | the index wraps to 0-63 | [#107](https://github.com/synpoox/pd2-resurrected/issues/107) |  |
