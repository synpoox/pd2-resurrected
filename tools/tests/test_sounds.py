"""pd2_sounds on synthetic tables and WAVs (no PD2 or D2R data)."""
import io
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_sounds as S  # noqa: E402

D2R = ("Sound|*Index|Redirect|Channel|FileName|Volume Min|Volume Max|Group Size|Priority|3dSpread\r\n"
       "|0|||none.flac|0|0|0|0|0\r\n"
       "izual_attack_hd1|1||sfx/monster_hd|monster\\izual\\attack1_hd.flac|255|255|4|80|90\r\n")
PD2 = ("Sound|Index|FileName|Volume|Group Size|Priority|Reverb\r\n"
       "zhar_attack1|4710|pd2\\zhar\\zhar_attack1.wav|200|0|70|1\r\n"
       "zhar_cast1|4711|pd2\\zhar\\zhar_cast1.wav|255|0|80|1\r\n")


def wav_bytes(samples: list[int], rate: int = 22050) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(s.to_bytes(2, "little", signed=True) for s in samples))
    return buf.getvalue()


def setup(tmp: Path, rule: str) -> tuple[Path, Path, Path, Path]:
    pd2, d2r, out, path = tmp / "pd2", tmp / "d2r", tmp / "out", tmp / "sounds.tsv"
    for root, text in ((pd2, PD2), (d2r, D2R)):
        (root / "global" / "excel").mkdir(parents=True)
        (root / "global" / "excel" / "sounds.txt").write_bytes(text.replace("|", "\t").encode("latin-1"))
    (pd2 / S.SFX / "pd2" / "zhar").mkdir(parents=True)
    (pd2 / S.SFX / "pd2" / "zhar" / "zhar_attack1.wav").write_bytes(wav_bytes([0, 1, -1, 32767, -32768] * 2000))
    out.mkdir()
    path.write_text(rule, "utf-8")
    return pd2, d2r, out, path


class FlacTests(unittest.TestCase):
    def test_frames_carry_the_samples_and_valid_crcs(self):
        f = S.flac(wav_bytes([0, 1, -1, 32767, -32768] * 2000))  # 10000 samples: frames of 4096, 4096, 1808
        self.assertEqual(f[:4], b"fLaC")
        self.assertEqual(int.from_bytes(f[8 + 10:8 + 18], "big") & ((1 << 36) - 1), 10000)  # total samples
        frames = f[42:].split(b"\xff\xf8")[1:]  # no sample pair here is ff f8
        self.assertEqual(len(frames), 3)
        for fr in frames:
            self.assertEqual(S._crc(b"\xff\xf8" + fr, 0x8005, 16), 0)  # a frame with its own CRC-16 sums to 0
        self.assertEqual(frames[0][7:11], b"\x00\x00\x00\x01")  # verbatim, big-endian: 0, 1
        self.assertEqual(S._utf8(0x7F), b"\x7f")
        self.assertEqual(S._utf8(0x80), b"\xc2\x80")


class RuleTests(unittest.TestCase):
    def test_listed_sound_appends_with_like_columns_and_a_flac(self):
        with tempfile.TemporaryDirectory() as tmp:
            pd2, d2r, out, rule = setup(Path(tmp), "# sound\tlike\nzhar_attack1\tizual_attack_hd1\n")
            S.apply(rule, pd2, d2r, out)
            rows = (out / "global" / "excel" / "sounds.txt").read_text("latin-1").splitlines()
            self.assertEqual(rows[:3], D2R.replace("|", "\t").splitlines())  # D2R's rows untouched
            self.assertEqual(rows[3], "zhar_attack1\t2\t\tsfx/monster_hd\tpd2\\zhar\\zhar_attack1.flac\t200\t200\t0\t70\t90")
            self.assertEqual((out / S.HD_SFX / "pd2" / "zhar" / "zhar_attack1.flac").read_bytes()[:4], b"fLaC")

    def test_bad_lines_fail(self):
        for line, msg in [("izual_attack_hd1\tizual_attack_hd1", "PD2 lacks"), ("zhar_attack1\tnope", "no row"),
                          ("zhar_cast1\tizual_attack_hd1", "no global"), ("zhar_attack1", "want")]:
            with tempfile.TemporaryDirectory() as tmp, self.subTest(line=line):
                pd2, d2r, out, rule = setup(Path(tmp), line + "\n")
                with self.assertRaisesRegex(S.SoundError, msg):
                    S.apply(rule, pd2, d2r, out)


if __name__ == "__main__":
    unittest.main()
