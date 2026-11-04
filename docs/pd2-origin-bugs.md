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
