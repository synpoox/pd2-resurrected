# Invader portal particle colours

For [#2193](https://github.com/synpoox/pd2-resurrected/issues/2193), the Invader unit uses a recolour of `vfx_town_portal_NewStuff_NewRed.particles` extracted from D2R 3.4 build 95032. Both paths in `invaderportal.json` point to `vfx_invader_portal_black.particles`. The unit's model, hit test and selection data are outside the change.

## Decoded fields

The five stock portal effects are PopcornFX 2.9.5 containers with magic `0xCA000B11`. All five round-tripped byte-for-byte through the [upstream HD effects decoder](https://github.com/Nugwa628/d2rAI-toolkit/tree/4029d2dc6c986243fab55fab569c3ea61d7c4018/hdkit/fx). The project's build and game tools use the repository's existing toolkit pin.

| Stock variant | Bytes | Objects | PC layers | PC renderers |
| --- | ---: | ---: | ---: | ---: |
| NewStuff | 699,806 | 6,421 | 45 | 26 |
| NewStuff_NewRed | 667,587 | 6,104 | 45 | 26 |
| NewStuff_RedVersion | 531,341 | 4,970 | 43 | 27 |

- `CLayerCompileCacheRendererParticleInput` links `Color.Color` and `Light.Color` to float4 layer fields. Field 4 of `CLayerCompileCacheField` is the constant slot when present and different from `0xffffffff`. Each float4 slot in field 0 of `CLayerCompileCache` occupies 32 bytes, with two copies of the four little-endian floats. `light` uses slot 1 in New Red, with approximately `(1, 0.202, 0.065, 1)` versus `(0.065, 0.235, 1, 1)` in NewStuff. `portalBack` already has `(0, 0, 0, 1)` in slot 4.
- Other fields are written by PopcornFX CPU VM scripts. For example, `New Wavelets` and `Overlapping Wavelets` feed `Color.Color` from float4 stream `15` in their respective layers. Stream identifiers are local to a layer. The recolour matches each renderer binding to its own scripts, then inserts a multiply by `(0, 0, 0, 1)` at each corresponding colour store. It zeros RGB in the constant-backed fields and their compiled copies as well. The edit covers each distinct graph referenced by the PC, Switch, PS4 and PS5 entries.
- Some surfaces sample `GradientRemap.GradientMap`. The shader applies particle colour after sampling the gradient. The edit retains the texture paths and animated alpha. Additive blending contributes no visible colour at zero RGB. `Transparency.Type = 2` enables alpha blending for `portalBack`, `portalEdge-Pulse`, `Russels Waves START` and `New Wavelets`, drawing the black centre and rim over the scene.

## Evidence

Matched offline renders at 0.5, 2 and 5 seconds showed the red flare and ring in the stock effect. Zeroing RGB removed the flare and left a faint ring. Alpha blending the four shape layers produced a black centre with an animated dark rim. The edited container and its scripts round-tripped byte-for-byte, and the offline VM ran the stock and edited variants.
