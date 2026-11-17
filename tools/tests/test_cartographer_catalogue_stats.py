"""#1331: every stat of Cartographer's built-in map catalogue has an itemstatcost row and a properties row here, at its fixed id.

Cartographer's installer refuses stat ids of 511 or more, so PD2R places the stats itself (ids 573-636, after cartographer_map_density 572).
"""
import unittest

import data_tables

# (stat, property, id): copied from d2r-tools plugins/cartographer/tools/install.ps1 $MonsterStats (commit 287c7fa33c3aa6d17e5a42bb615291e82321378e),
# the catalogue after the first eight stats (cartographer_map_level .. cartographer_map_density, ids 564-572), in that order.
CATALOGUE = [
    ("cartographer_mon_tohit", "mon-tohit", 573),
    ("cartographer_mon_tohit_pct", "mon-tohit-pct", 574),
    ("cartographer_mon_armor_pct", "mon-armor-pct", 575),
    ("cartographer_mon_dmg_fire_min", "mon-dmg-fire-min", 576),
    ("cartographer_mon_dmg_fire_max", "mon-dmg-fire-max", 577),
    ("cartographer_mon_dmg_ltng_min", "mon-dmg-ltng-min", 578),
    ("cartographer_mon_dmg_ltng_max", "mon-dmg-ltng-max", 579),
    ("cartographer_mon_dmg_cold_min", "mon-dmg-cold-min", 580),
    ("cartographer_mon_dmg_cold_max", "mon-dmg-cold-max", 581),
    ("cartographer_mon_dmg_cold_len", "mon-dmg-cold-len", 582),
    ("cartographer_mon_dmg_mag_min", "mon-dmg-mag-min", 583),
    ("cartographer_mon_dmg_mag_max", "mon-dmg-mag-max", 584),
    ("cartographer_mon_dmg_pois_min", "mon-dmg-pois-min", 585),
    ("cartographer_mon_dmg_pois_max", "mon-dmg-pois-max", 586),
    ("cartographer_mon_dmg_pois_len", "mon-dmg-pois-len", 587),
    ("cartographer_mon_mastery_fire", "mon-mastery-fire", 588),
    ("cartographer_mon_mastery_ltng", "mon-mastery-ltng", 589),
    ("cartographer_mon_mastery_cold", "mon-mastery-cold", 590),
    ("cartographer_mon_mastery_pois", "mon-mastery-pois", 591),
    ("cartographer_mon_mastery_mag", "mon-mastery-mag", 592),
    ("cartographer_mon_pierce_fire", "mon-pierce-fire", 593),
    ("cartographer_mon_pierce_ltng", "mon-pierce-ltng", 594),
    ("cartographer_mon_pierce_cold", "mon-pierce-cold", 595),
    ("cartographer_mon_pierce_pois", "mon-pierce-pois", 596),
    ("cartographer_mon_absorb_fire", "mon-absorb-fire", 597),
    ("cartographer_mon_absorb_fire_pct", "mon-absorb-fire-pct", 598),
    ("cartographer_mon_absorb_ltng", "mon-absorb-ltng", 599),
    ("cartographer_mon_absorb_ltng_pct", "mon-absorb-ltng-pct", 600),
    ("cartographer_mon_absorb_cold", "mon-absorb-cold", 601),
    ("cartographer_mon_absorb_cold_pct", "mon-absorb-cold-pct", 602),
    ("cartographer_mon_absorb_mag", "mon-absorb-mag", 603),
    ("cartographer_mon_absorb_mag_pct", "mon-absorb-mag-pct", 604),
    ("cartographer_mon_dr", "mon-dr", 605),
    ("cartographer_mon_mdr", "mon-mdr", 606),
    ("cartographer_mon_dr_pct", "mon-dr-pct", 607),
    ("cartographer_mon_mag_res", "mon-mag-res", 608),
    ("cartographer_mon_deadly", "mon-deadly", 609),
    ("cartographer_mon_crush", "mon-crush", 610),
    ("cartographer_mon_openwounds", "mon-openwounds", 611),
    ("cartographer_mon_thorns", "mon-thorns", 612),
    ("cartographer_mon_attackrate", "mon-attackrate", 613),
    ("cartographer_mon_far", "mon-far", 614),
    ("cartographer_mon_fcr", "mon-fcr", 615),
    ("cartographer_mon_fhr", "mon-fhr", 616),
    ("cartographer_mon_fbr", "mon-fbr", 617),
    ("cartographer_mon_regen", "mon-regen", 618),
    ("cartographer_mon_cannotfreeze", "mon-cannotfreeze", 619),
    ("cartographer_mon_halffreeze", "mon-halffreeze", 620),
    ("cartographer_mon_curse_res", "mon-curse-res", 621),
    ("cartographer_mon_missile_pierce", "mon-missile-pierce", 622),
    ("cartographer_map_area_level", "map-area-level", 623),
    ("cartographer_map_uniques", "map-uniques", 624),
    ("cartographer_map_extra_groups", "map-extra-groups", 625),
    ("cartographer_map_extra_boss", "map-extra-boss", 626),
    ("cartographer_map_drop_tc", "map-drop-tc", 627),
    ("cartographer_map_drop_ethereal", "map-drop-ethereal", 628),
    ("cartographer_map_drop_socketed", "map-drop-socketed", 629),
    ("cartographer_map_drop_bonus", "map-drop-bonus", 630),
    ("cartographer_mon_lifesteal_pct", "mon-lifesteal-pct", 631),
    ("cartographer_mon_phys_as_fire", "mon-phys-as-fire", 632),
    ("cartographer_mon_phys_as_cold", "mon-phys-as-cold", 633),
    ("cartographer_mon_phys_as_ltng", "mon-phys-as-ltng", 634),
    ("cartographer_mon_phys_as_pois", "mon-phys-as-pois", 635),
    ("cartographer_mon_phys_as_mag", "mon-phys-as-mag", 636),
]


class CartographerCatalogueStatsTests(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_64_stats_at_573_onward(self):
        assert len(CATALOGUE) == 64 and [c[2] for c in CATALOGUE] == list(range(573, 637))

    def test_every_catalogue_stat_has_a_row_at_its_id_and_a_property(self):
        stats = data_tables.rows("itemstatcost")
        props = {r["code"]: r for r in data_tables.rows("properties")}
        for stat, prop, sid in CATALOGUE:
            assert stats[sid]["Stat"] == stat and stats[sid]["*ID"] == str(sid), stat
            assert props[prop]["stat1"] == stat, prop
            assert stats[sid]["descstrpos"] == "cartographer:" + stat[len("cartographer_"):], stat

    def test_ids_list_pins_them(self):
        pinned = {}
        for line in (data_tables.ROOT / "merge/ids-itemstatcost.tsv").read_text().splitlines():
            if line and not line.startswith("# "):
                _, n, name = line.split("\t")
                pinned[name] = int(n)
        for stat, _, sid in CATALOGUE:
            assert pinned[stat] == sid, stat
