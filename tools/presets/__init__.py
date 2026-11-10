"""The HD preset generator (LP3, ADR 0005), split into modules; tools/pd2_presets.py is its entry point and docstring.

consts, dt1, entity, layout: the constants, the DT1 and DS1 readers and the Layout.
kit, hooks/: the Kit of stock pieces by tile key; Kit.add's filters are discovered hooks.
place, rooms, patches, waypoints, water: the steps build() uses.
height, passes/height: the ground height per tile (#742); later passes read it with passes.height.ground_at(b, tile).
build, passes/: build() runs the discovered passes in their ORDER.
apply: the pd2_merge rule.

Passes and hooks are found by name in their packages, each with its own ORDER: add a module, edit no shared list.
"""
