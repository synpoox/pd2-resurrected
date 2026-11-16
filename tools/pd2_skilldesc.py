"""skilldesc's description-line mapping (#73): PD2's classic line functions onto D2R's, applied by pd2_tables after the merge.

The rewrite itself is d2rAI-toolkit's shared/classic_import skilldesc; the owner-approved mapping (#73) stays here.
LINES, FIXED, KEYS, BUILTINS and BELOW are skilldesc.Mapping's fields (see its module docstring).
"""
from __future__ import annotations

import pd2_table  # noqa: F401  (first: puts the toolkit on sys.path)
from shared.classic_import.skilldesc import Mapping

# PD2 function -> D2R function, and how text a/b and calc a/b are rebuilt (key: the line's D2R string with a format
# code, after KEYS). "d2r": text a/b as a D2R line of that function lays the key out; "swap": PD2's text b, then
# text a (synergy: format, then skill name); "b": PD2's text b alone; "copy": the skill's own D2R line naming the key,
# calcs included
LINES = {
    **{f: ("74", "d2r", lambda a, b: (a, "")) for f in ("2", "3", "4", "5", "7", "51", "66")},
    "73": ("74", "d2r", lambda a, b: (f"({a})*100/({b})", "")),
    **{f: ("75", "d2r", lambda a, b: (a, b)) for f in ("38", "35", "43", "47", "52", "59")},
    "62": ("75", "b", lambda a, b: (a, b)),
    "63": ("76", "swap", lambda a, b: (a, "")), "67": ("76", "swap", lambda a, b: (a, "")),
    "12": ("36", "d2r", lambda a, b: (a, "25")),  # frames -> seconds
    "19": ("36", "d2r", lambda a, b: (f"({a})*2", "3")),  # PD2 prints a*2/3 yards
    "37": ("36", "d2r", lambda a, b: (a, "3")),
    **{f: (None, "copy", None) for f in ("23", "24", "30", "31")},  # no calc of their own (missile values)
}
# whole lines, by PD2 function and text a
FIXED = {("1", ""): ("36", "StrSkill3", "", "usmc", "256"),
         ("49", "ItemStats1o"): ("75", "StrSkillSmiteDamage", "", "pnma", "pxma"),  # PD2's built-in smite damage
         ("5", "StrSkill3"): ("36", "StrSkill3", "", "usmc*2", "256"),  # PD2: usmc/128
         ("61", "StrSkill3"): ("36", "EskillPerBlade", "", "usmc", "256"),
         ("70", "StrSkeleAll"): ("74", "StrSkill96", "StrSkill42", None, ""),  # None: PD2's calc a
         ("70", "StrSkeleAllPhys"): ("74", "StrSkillSleketonMasterySkeletonDamage", "", None, "")}
# D2R reused these keys with a range ("Damage: %d-%d") or another suffix; by PD2 function and text a
KEYS = {"2": {"StrSkill4": "StrSkillDamagePercent", "StrSkill5": "StrSkillFireDamagePercent",
              "StrSkill6": "StrSkillColdDamagePercent", "StrSkill7": "StrSkillLightningDamagePercent",
              "StrSkill39": "StrSkillMagicDamagePercent", "StrSkill8": "StrSkillPoisonDamagePercent"},
        "3": {"StrSkill3": "StrSkillManaCostPerSecond", "OpenWoundsDamage": "OpenWoundsDamagePerSecond",
              "PartyOpenWoundsDamage": "PartyOpenWoundsDamagePerSecond",
              "ThornsOpenWoundsDamage": "ThornsOpenWoundsDamagePerSecond"},
        "4": {"StrSkill4": "StrSkillDamageFlat", "StrSkill50": "StrSkillHealFlat"},
        "52": {"StrSkill89": "StrSkillRunWalkRange", "StrSkill106": "StrSkillAttackSpeedRange",
               "EskillLifeSteal": "StrSkillLifeStealRange", "StrSkill4": "StrSkillDamagePercentRange"}}
# PD2's built-in lines (no text: the game picks the label from the skill) -> the D2R labels that line becomes, by the
# skill's element (skills EType; "" any): attack rating, physical damage, elemental damage, elemental length, poison
# damage, burning damage. The skill's own D2R line with one of the function's labels is copied (PD2's EType is not
# always the shown element: frost nova's is fire), else D2R's most common line with the element's label; a built-in
# with no label for the skill's element (11 on a lightning skill: PD2 prints no length) is left empty
_DAMAGE = {"fire": "StrSkill5", "cold": "StrSkill6", "ltng": "StrSkill7", "mag": "StrSkill39", "pois": "StrSkill8"}
BUILTINS = {"8": {"": "StrSkill22"}, "9": {"": "StrSkill4"}, "10": _DAMAGE,
            "11": {"cold": "StrSkillColdLengthSingular", "pois": "StrSkillPoisonLengthSingular"},
            "14": {"": "StrSkill8"}, "22": {"": "StrSkillFireDamageAveragePerSecond"},
            # damage per second, burning per second, fire wall's size, golem damage, magic damage
            "26": {"fire": "StrSkillFireDamageAveragePerSecond", "cold": "StrSkillColdDamageAveragePerSecond",
                   "ltng": "StrSkillLightningDamageAveragePerSecond"},
            "27": {"": "StrSkillFireDamageAveragePerSecond"}, "29": {"": "StrSkillRangeSingular"},
            "39": {"": "StrSkill4"}, "48": {"": "StrSkill39"}}
# built-ins PD2 prints as one line and D2R as two: the label of D2R's line under it (poison damage "over N seconds"),
# put in the built-in's slot with the built-in and the lines above it moving up one (the tooltip stacks lines upward)
BELOW = {"14": "StrSkillPoisonLengthSingular"}

MAPPING = Mapping(lines=LINES, fixed=FIXED, keys=KEYS, builtins=BUILTINS, below=BELOW)
