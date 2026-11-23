"""Verify RuffnecKk's optional Ascendant assets and merge their model registrations.

No toolkit, game launch or installed-profile writes. See docs/contributions/ascendant-assets.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "art" / "ascendants"
STRIDES = (44, 12, 32)


def model_key(path: str) -> int:
    key = 2166136261
    for byte in path.lower().encode("ascii"):
        key = ((key ^ byte) * 16777619) & 0xFFFFFFFF
    return key


def decode(data: bytes) -> dict[int, dict]:
    """Read each model with its owned LOD/mesh records; preserve opaque bytes."""
    if not 24 <= len(data) <= 64 * 1024 * 1024:
        raise ValueError("invalid catalog size")
    counts, starts = [], []
    for i in range(3):
        count, offset = struct.unpack_from("<II", data, i * 8)
        counts.append(count)
        starts.append(i * 8 + 4 + offset)
    ends = [start + count * stride for start, count, stride in zip(starts, counts, STRIDES)]
    if starts != [24, ends[0], ends[1]] or ends[2] != len(data):
        raise ValueError("unsupported catalog array layout")
    used = [set(), set()]

    def children(pointer, count, array):
        target = pointer + struct.unpack_from("<I", data, pointer)[0]
        delta = target - starts[array]
        if delta < 0 or delta % STRIDES[array] or delta // STRIDES[array] + count > counts[array]:
            raise ValueError("invalid catalog child range")
        first = delta // STRIDES[array]
        owned = set(range(first, first + count))
        if used[array - 1] & owned:
            raise ValueError("overlapping catalog children")
        used[array - 1].update(owned)
        return [starts[array] + i * STRIDES[array] for i in range(first, first + count)]

    models = {}
    previous = -1
    for p in range(starts[0], ends[0], 44):
        raw = bytearray(data[p:p + 44])
        key, = struct.unpack_from("<I", raw)
        if key <= previous:
            raise ValueError("model keys must be unique and sorted")
        previous = key
        lods = []
        for q in children(p + 36, struct.unpack_from("<I", raw, 32)[0], 1):
            meshes = children(q + 8, struct.unpack_from("<I", data, q + 4)[0], 2)
            lods.append({"record": data[q:q + 8].hex(),
                         "meshes": [data[r:r + 32].hex() for r in meshes]})
        raw[36:40] = bytes(4)
        models[key] = {"record": raw.hex(), "lods": lods}
    if any(len(seen) != counts[i + 1] for i, seen in enumerate(used)):
        raise ValueError("unreferenced catalog children")
    return models


def encode(models: dict[int, dict]) -> bytes:
    ordered = sorted(models.items())
    lod_count = sum(len(model["lods"]) for _, model in ordered)
    mesh_count = sum(len(lod["meshes"]) for _, model in ordered for lod in model["lods"])
    starts = (24, 24 + len(ordered) * 44, 24 + len(ordered) * 44 + lod_count * 12)
    result = bytearray(starts[2] + mesh_count * 32)
    if len(result) > 64 * 1024 * 1024:
        raise ValueError("catalog exceeds size limit")
    for i, count in enumerate((len(ordered), lod_count, mesh_count)):
        struct.pack_into("<II", result, i * 8, count, starts[i] - (i * 8 + 4))
    li = mi = 0
    for i, (key, model) in enumerate(ordered):
        p = starts[0] + i * 44
        raw = bytes.fromhex(model["record"])
        if len(raw) != 44 or struct.unpack_from("<I", raw)[0] != key:
            raise ValueError("invalid model record")
        if struct.unpack_from("<I", raw, 32)[0] != len(model["lods"]):
            raise ValueError("model LOD count mismatch")
        result[p:p + 44] = raw
        struct.pack_into("<I", result, p + 36, starts[1] + li * 12 - (p + 36))
        for lod in model["lods"]:
            q = starts[1] + li * 12
            raw_lod = bytes.fromhex(lod["record"])
            if len(raw_lod) != 8 or struct.unpack_from("<I", raw_lod, 4)[0] != len(lod["meshes"]):
                raise ValueError("invalid LOD record")
            result[q:q + 8] = raw_lod
            struct.pack_into("<I", result, q + 8, starts[2] + mi * 32 - (q + 8))
            for mesh in lod["meshes"]:
                raw_mesh = bytes.fromhex(mesh)
                if len(raw_mesh) != 32:
                    raise ValueError("invalid mesh record")
                r = starts[2] + mi * 32
                result[r:r + 32] = raw_mesh
                mi += 1
            li += 1
    encoded = bytes(result)
    if decode(encoded) != models:
        raise ValueError("catalog round trip differs")
    return encoded


def merge_catalog(data: bytes, registrations: dict) -> bytes:
    original = decode(data)
    merged = dict(original)
    for path, model in registrations.items():
        key = model_key(path)
        if key in merged and merged[key] != model:
            raise ValueError(f"conflicting model registration or hash collision: {path}")
        merged[key] = model
    # Keep an already-integrated catalog byte-for-byte, including child order.
    if merged == original:
        return data
    result = encode(merged)
    checked = decode(result)
    if any(checked[key] != value for key, value in original.items()):
        raise ValueError("existing catalog entries changed")
    return result


def merge_textures(original: list[dict], additions: list[dict]) -> list[dict]:
    merged = list(original)
    by_name = {}
    for item in original:
        name = item["filename"].lower()
        if name in by_name:
            raise ValueError(f"duplicate texture descriptor: {name}")
        by_name[name] = item
    for item in additions:
        name = item["filename"].lower()
        if name in by_name:
            if by_name[name] != item:
                raise ValueError(f"conflicting texture descriptor: {name}")
        else:
            merged.append(item)
            by_name[name] = item
    return merged


def merge_metadata(original: dict, additions: dict) -> dict:
    merged = dict(original)
    for name, entry in additions.items():
        if name in merged and merged[name] != entry:
            raise ValueError(f"conflicting model metadata: {name}")
        merged[name] = entry
    return merged


def verify(root: Path = ROOT) -> int:
    manifest = json.loads((root / "art/ascendants/manifest.json").read_text("utf-8"))
    for entry in manifest["files"]:
        data = (root / entry["path"]).read_bytes()
        if len(data) != entry["bytes"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise ValueError(f"asset differs: {entry['path']}")
    return len(manifest["files"])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, help="recipient's effective model_lod_desc.bin")
    parser.add_argument("--output", type=Path, help="fresh output catalog; never overwrites an existing file")
    parser.add_argument("--texture-cache", type=Path, help="recipient's effective texture_desc_cache.json")
    parser.add_argument("--texture-output", type=Path, help="fresh output texture cache")
    parser.add_argument("--lod-json", type=Path, help="recipient's optional model_lod_desc.json")
    parser.add_argument("--lod-output", type=Path, help="fresh output authoring metadata")
    args = parser.parse_args()
    pairs = [(args.catalog, args.output), (args.texture_cache, args.texture_output),
             (args.lod_json, args.lod_output)]
    for source, output in pairs:
        if bool(source) != bool(output):
            parser.error("each input needs its matching output")
        if output and output.exists():
            parser.error(f"output exists; choose a fresh path: {output}")
    outputs = [output.resolve() for _, output in pairs if output]
    if len(outputs) != len(set(outputs)):
        parser.error("outputs must be distinct")
    count = verify()
    registrations = json.loads((ART / "model-registrations.json").read_text("utf-8"))
    pending = []
    if args.catalog:
        data = merge_catalog(args.catalog.read_bytes(), registrations)
        pending.append((args.output, data))
    if args.texture_cache:
        entries = json.loads(args.texture_cache.read_text("utf-8-sig"))
        additions = json.loads((ART / "texture-registration.json").read_text("utf-8"))
        pending.append((args.texture_output, (json.dumps(merge_textures(entries, additions), indent=2) + "\n").encode()))
    if args.lod_json:
        entries = json.loads(args.lod_json.read_text("utf-8-sig"))
        additions = json.loads((ART / "model-lod-metadata.json").read_text("utf-8"))
        pending.append((args.lod_output, (json.dumps(merge_metadata(entries, additions), indent=2) + "\n").encode()))
    for path, data in pending:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as output:
            output.write(data)
        print(f"Wrote merged descriptors: {path}")
    print(f"Verified {count} contributed files. Assets are optional; appearance/portrait selection is not activated.")


if __name__ == "__main__":
    main()
