"""The inventory padlock sprites (tools/make_inventory_lock.py): 4 frames at 72 px HD and 36 px lowend, none empty."""
import struct
import unittest
from pathlib import Path

PANEL = Path(__file__).resolve().parents[2] / "files/data/hd/global/ui/panel/pd2r"
HEAD = struct.Struct("<4sHHIIIIIIII")  # D2R .sprite version 31 (modkit images.py)


class InventoryLockArt(unittest.TestCase):
    def test_frames(self):
        for name, size in (("inventory_lock.sprite", 72), ("inventory_lock.lowend.sprite", 36)):
            data = (PANEL / name).read_bytes()
            magic, version, fw, width, height, _, frames, _, _, length, _ = HEAD.unpack_from(data)
            self.assertEqual((magic, version, fw, width, height, frames), (b"SpA1", 31, size, size * 4, size, 4), name)
            rgba = data[HEAD.size:HEAD.size + length]
            self.assertEqual(len(rgba), width * height * 4, name)
            slices = [b"".join(rgba[(y * width + i * size) * 4:(y * width + (i + 1) * size) * 4] for y in range(height))
                      for i in range(frames)]
            for i, f in enumerate(slices):
                self.assertGreater(sum(1 for a in f[3::4] if a), size * size // 2, f"{name} frame {i} is empty")
            self.assertEqual(len(set(slices)), 4, f"{name}: every frame differs")


if __name__ == "__main__":
    unittest.main()
