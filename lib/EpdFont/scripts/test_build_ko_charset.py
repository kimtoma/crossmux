from pathlib import Path
import re
import unittest
import yaml

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
KS = ROOT / "chars_ko_ks1001_2350.txt"
YAML = REPO / "lib/I18n/translations/korean.yaml"


def codepoints(path: Path) -> set[int]:
    text = path.read_text(encoding="utf-8")
    return {ord(ch) for ch in text if not ch.isspace()}


def header_codepoints(size: int) -> set[int]:
    text = (REPO / f"lib/EpdFont/builtinFonts/notosans_ko_{size}.h").read_text(encoding="utf-8")
    match = re.search(
        rf"static const EpdUnicodeInterval notosans_ko_{size}Intervals\[\] = \{{(.*?)\n\}};",
        text,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing notosans_ko_{size} interval table")
    intervals = re.findall(
        r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*0x[0-9A-Fa-f]+\s*\}",
        match.group(1),
    )
    return {cp for start, end in intervals for cp in range(int(start, 16), int(end, 16) + 1)}


def ui_codepoints() -> set[int]:
    data = yaml.safe_load(YAML.read_text(encoding="utf-8"))
    return {ord(ch) for value in data.values() if isinstance(value, str) for ch in value if not ch.isspace()}


class KoreanCharsetTest(unittest.TestCase):
    def test_ks_x_1001_pool_is_exact(self):
        chars = codepoints(KS)
        self.assertEqual(2350, len(chars))
        self.assertTrue(all(0xAC00 <= cp <= 0xD7A3 for cp in chars))

    def test_small_sizes_cover_pool_and_ui(self):
        required = codepoints(KS) | ui_codepoints()
        for size in (8, 10, 12):
            with self.subTest(size=size):
                self.assertTrue(required <= header_codepoints(size))

    def test_all_sizes_cover_ui(self):
        required = ui_codepoints()
        for size in (8, 10, 12, 14, 16, 18):
            with self.subTest(size=size):
                self.assertTrue(required <= header_codepoints(size))


if __name__ == "__main__":
    unittest.main()
