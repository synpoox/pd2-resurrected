"""Vendors refuse what PD2's vendors refuse (#2012): the code lists in core-rules' item_prices.cpp are PD2's (ProjectDiablo.dll
0x1037f760) and every code is a row of our tables."""
import csv
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables as D  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "plugin" / "core-rules" / "item_prices.cpp"
PD2 = ROOT / "pd2-data" / "data" / "global" / "excel"

# PD2's compares in 0x1037f760, as read from the DLL
TYPE_IDS = {20: "gem", 74: "rune", 148: "rung", 211: "t5m", 38: "tpot", 42: "tkni", 43: "taxe", 44: "jave", 48: "thro",
            56: "misl", 87: "ajav"}
STACK_EXEMPT = ["tbk", "ibk", "aqv", "cqv", "aqv2", "cqv2", "aqv3", "cqv3", "key", "rps", "rpl", "bps", "bpl"]
LISTED = ["ubtm", "dcma", "rtma", "uba", "llmr", "lsvl", "rkey", "rid", "rtp"]
UNIQUE = ["cm1", "cm2"]


def cpp_codes(name: str) -> list[str]:
    body = re.search(rf"constexpr uint32_t {name}\[\] = \{{(.*?)\}};", SOURCE.read_text(encoding="utf-8"), re.S).group(1)
    return ["".join(re.findall(r"'(.)'", call)).rstrip() for call in re.findall(r"MakeFourCC\(([^)]*)\)", body)]


class VendorSellRefusals(unittest.TestCase):
    def test_the_type_list_is_pd2s(self):
        self.assertEqual(cpp_codes("TypeCodes"), [TYPE_IDS[i] for i in (20, 74, 148, 211, 38, 42, 43, 44, 48, 56, 87)])

    def test_the_code_lists_are_pd2s(self):
        self.assertEqual(cpp_codes("StackExemptCodes"), STACK_EXEMPT)
        self.assertEqual(cpp_codes("ListedCodes"), LISTED)
        self.assertEqual(cpp_codes("UniqueCodes"), UNIQUE)

    def test_the_hook_finds_types_by_code_not_by_id(self):
        src = SOURCE.read_text(encoding="utf-8")
        self.assertIn("TableId::ItemTypes", src)
        self.assertIn("findRowByCode", src)

    def test_pd2s_type_ids_are_those_codes(self):
        if not PD2.is_dir():
            self.skipTest("no pd2-data: run tools/pd2_extract.py")
        with open(PD2 / "itemtypes.txt", encoding="latin-1", newline="") as f:
            rows = [r for r in csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE) if r["ItemType"] != "Expansion"]
        self.assertEqual({i: rows[i]["Code"] for i in TYPE_IDS}, TYPE_IDS)

    def test_every_code_is_a_row_of_our_tables(self):
        D.need(self)
        types = {r["Code"] for r in D.rows("itemtypes")}
        items = {r["code"] for t in ("misc", "weapons", "armor") for r in D.rows(t) if r.get("code")}
        self.assertEqual([c for c in cpp_codes("TypeCodes") if c not in types], [])
        self.assertEqual([c for c in STACK_EXEMPT + LISTED + UNIQUE if c not in items], [])

    def test_gems_and_runes_are_stackable_so_the_stack_rule_refuses_them(self):
        D.need(self)
        parents = {r["Code"]: (r["Equiv1"], r["Equiv2"]) for r in D.rows("itemtypes")}

        def under(code: str, root: str) -> bool:
            return code == root or any(p and under(p, root) for p in parents.get(code, ()))

        loose = [r["code"] for r in D.rows("misc") if r.get("code") and (under(r["type"], "gem") or under(r["type"], "rune"))
                 and r["stackable"] != "1"]
        self.assertEqual(loose, [])


if __name__ == "__main__":
    unittest.main()
