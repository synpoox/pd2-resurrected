"""Hot-path log lines are sampled (#1609): D2RLoader flushes each plugin line to disk (~5 ms) on the game thread."""
import re
import unittest
from pathlib import Path

PLUGIN = Path(__file__).resolve().parent.parent.parent / "plugin"
SAMPLER = re.compile(r"HotSample|plog::Sample|First\(|FirstUnitHit|FinisherSample|Logged\(|GateLog\(|HotLogsOn\(")
# A gate that logs a unit kind every time before it samples: `if (type == 0 || pd2::HotSample(n))`
BYPASS = re.compile(r"\|\|\s*[\w:]*?(?:" + SAMPLER.pattern + ")")
# Functions that run per hit, kill, drop, missile or curse: every log call in them sits under a sampler
HOT = {
    "core-rules/item_rules.cpp": ["LogStrike", "MissileStrike", "HitEvent", "CappedAddLife", "CrushingBlow", "OpenWounds",
                                  "RefreshOpBase", "MakeRoom", "ResistCurse", "CurseGate", "ApplyUngated", "CurseApply",
                                  "ApplyDarkPact", "SetPhasing", "EquipStatCallback", "EquipAuraTick", "AddCharge",
                                  "MapItemMaker"],
    "core-rules/loot_rules.cpp": ["ApplyEthereal", "RollStaffMods"],
    "core-rules/skill_functions.cpp": ["AvoidWithParry", "SrvDoMultipleShot", "MeleeHit", "LeapHitOne", "QueueHitHook"],
    "core-rules/missile_functions.cpp": ["SrvHitSpawnReturn", "SrvHitBounce", "SrvDmgFirstHit", "SrvHitExplodeSpread",
                                         "SrvHitState", "SrvHitBoltSplash", "SrvHitUberImmolation", "CltHitHolyLight",
                                         "CltHitFistDelay", "CltHitChargeBolt", "SrvHitBladeFragment",
                                         "CltHitBladeFragment", "SrvHitSummon", "SrvHitThrowPotion",
                                         "CltHitThrowPotion"],
    # per map open (#1593): all but the one `rolled event` line sit under HotLogsOn / HotSample
    "core-events/map_events.cpp": ["RollNative", "BeforeRoom", "AfterRoom", "UseOneOfStack", "OnUseItem"],
    "core-events/uber_scaling.cpp": ["ScaleUberMonster"],
    "core-events/rathma_life.cpp": ["SharedStep", "SharedPoison"],
}


def bodies(text: str, name: str) -> list[str]:
    """Each top-level definition of `name` (its lines from the signature to the closing brace)."""
    out = []
    for m in re.finditer(rf"^(?:auto|void)\s+{name}\(", text, re.M):
        i = text.index("{", m.end())
        depth = 0
        for j in range(i, len(text)):
            depth += {"{": 1, "}": -1}.get(text[j], 0)
            if depth == 0:
                out.append(text[m.start():j + 1])
                break
    return out


def enclosing_if(lines: list[str], i: int) -> str:
    """The condition line of the `if` block that holds line i ('' when none)."""
    indent = len(lines[i]) - len(lines[i].lstrip("\t"))
    for line in reversed(lines[:i]):
        if line.strip() and len(line) - len(line.lstrip("\t")) < indent:
            return line if re.match(r"\s*(?:\} else )?if \(", line) else ""
    return ""


class HotPathLogging(unittest.TestCase):
    def test_no_dense_modulus_gate(self):
        """A `n <= N || n % M == 0` gate logs at most one in 500 after its first lines."""
        for f in PLUGIN.rglob("*.cpp"):
            if "tests" in f.parts:
                continue
            for m in re.finditer(r"<= \d+ \|\| \w+ % (\d+) == 0", f.read_text(encoding="utf-8")):
                self.assertGreaterEqual(int(m.group(1)), 500, f"{f.name}: {m.group(0)}")

    def test_bypass_pattern(self):
        self.assertRegex("if (type == 0 || pd2::HotSample(n)) {", BYPASS)
        self.assertNotRegex("if (pd2::HotSample(n)) {", BYPASS)

    def test_every_sampler_checks_the_debug_flag(self):
        """Hot-path lines write nothing in normal play (#1609): each sampler needs `PD2R_HOT_LOGS` (pd2::HotLogsOn)."""
        hot = (PLUGIN / "common/hot_log.h").read_text(encoding="utf-8")
        self.assertEqual(len(re.findall(r"HotSample\(.*\{", hot)), 2)
        self.assertRegex(hot.split("auto HotSample(uint32_t")[1].split("}")[0], r"&& HotLogsOn\(\)")
        self.assertRegex(hot.split("auto HotSample(std::atomic")[1].split("}")[0], r"return HotSample\(\+\+counter\)")
        for rel, helper in [("core-rules/skill_functions.cpp", "First"), ("core-rules/missile_functions.cpp", "FirstUnitHit")]:
            body = bodies((PLUGIN / rel).read_text(encoding="utf-8"), helper)
            self.assertTrue(body and "HotLogsOn()" in body[0], f"{rel}: {helper} skips the debug flag")
        for rel in HOT:
            text = (PLUGIN / rel).read_text(encoding="utf-8")
            self.assertNotRegex(text, r"HotSample\([^)]*\)\s*\|\|(?![^{;]*(?:HotSample|First|HotLogsOn|\w+First))",
                                f"{rel}: an `|| ...` after a sampler bypasses the flag")
        for rel in ["core-rules/missile_functions.cpp", "core-rules/skill_functions.cpp", "core-events/map_events.cpp"]:
            self.assertNotIn("plog::Sample(", (PLUGIN / rel).read_text(encoding="utf-8"), f"{rel}: use pd2::HotSample")

    def test_hot_functions_sample_every_log_call(self):
        for rel, names in HOT.items():
            text = (PLUGIN / rel).read_text(encoding="utf-8")
            for name in names:
                found = bodies(text, name)
                self.assertTrue(found, f"{rel}: no {name}")
                for body in found:
                    # flags set from a sampler (`const bool log = pd2::HotSample(n);`) count as one
                    flags = re.findall(r"bool (\w+) = [^;]*(?:" + SAMPLER.pattern + ")", body)
                    gate = re.compile("|".join([SAMPLER.pattern] + [rf"\b{f}\b" for f in flags]))
                    lines = body.split("\n")
                    for i, line in enumerate(lines):
                        if re.search(r"plog::(Info|Warn)\(", line) and "has no valid" not in line and not (rel == "core-events/map_events.cpp" and name in ("UseOneOfStack", "OnUseItem")
                                                           and re.search(r"refused|dropped|not used", line)):  # a bad row or a refused use, not a hit
                            # an `if (!sampled) return;` ahead of the line counts too
                            early = [p for p in lines[:i] if re.match(r"\s*if \(.*!", p) and gate.search(p)]
                            cond = enclosing_if(lines, i)
                            where = f"{rel} {name}: `{line.strip()[:80]}`"
                            self.assertRegex(cond + "".join(early), gate, f"unsampled {where}")
                            self.assertNotRegex(cond, BYPASS, f"logged past the sampler {where}")


if __name__ == "__main__":
    unittest.main()
