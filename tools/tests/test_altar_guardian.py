"""The Altar guardian's death runs on a native Cartographer map, whose level is in no Horazon tier (#954)."""
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "plugin" / "core-events"


class AltarGuardian(unittest.TestCase):
    def test_death_not_gated_on_tier(self):
        text = (SRC / "horazon.cpp").read_text(encoding="utf-8")
        self.assertNotIn("TierOf", text)
        self.assertIn("AltarGuardianDied", text)

    def test_console_forces_altar(self):
        self.assertIn('{"altar", Altar}', (SRC / "map_events.cpp").read_text(encoding="utf-8"))

    def test_console_forces_spire(self):
        self.assertIn('{"spire", Spire}', (SRC / "map_events.cpp").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
