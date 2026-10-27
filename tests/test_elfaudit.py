import importlib.util
import struct
import sys
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "rtm_elf_audit.py"
SPEC = importlib.util.spec_from_file_location("rtm_elf_audit", MODULE_PATH)
elfaudit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = elfaudit
SPEC.loader.exec_module(elfaudit)


def synthetic_elf():
    data = bytearray(0x240)
    data[:16] = b"\x7fELF" + bytes((2, 2, 1)) + bytes(9)
    struct.pack_into(
        ">HHIQQQIHHHHHH",
        data,
        16,
        2,
        21,
        1,
        0x10000,
        64,
        0,
        0,
        64,
        56,
        1,
        0,
        0,
        0,
    )
    struct.pack_into(
        ">IIQQQQQQ",
        data,
        64,
        1,
        5,
        0x200,
        0x10000,
        0,
        16,
        24,
        0x1000,
    )
    data[0x200:0x210] = bytes.fromhex("1122334438C6FFFF5566778838C0FFFF")
    return bytes(data)


class ELFAuditTests(unittest.TestCase):
    def test_maps_file_bytes_and_zero_filled_bss(self):
        image = elfaudit.ELFImage(synthetic_elf())
        self.assertEqual(image.read_virtual(0x10000, 4).hex().upper(), "11223344")
        self.assertEqual(image.read_virtual(0x10010, 8), bytes(8))
        self.assertEqual(image.read_virtual(0x1000C, 8).hex().upper(), "38C0FFFF00000000")

    def test_signature_search_only_returns_aligned_executable_addresses(self):
        image = elfaudit.ELFImage(synthetic_elf())
        self.assertEqual(image.find_aligned(bytes.fromhex("38C6FFFF")), [0x10004])
        self.assertEqual(image.find_aligned(bytes.fromhex("38C0FFFF")), [0x1000C])

    def test_rejects_bad_endian_and_truncated_program_headers(self):
        image = bytearray(synthetic_elf())
        image[5] = 1
        with self.assertRaises(elfaudit.ELFError):
            elfaudit.ELFImage(bytes(image))

        image = bytearray(synthetic_elf())
        struct.pack_into(">H", image, 18, 3)
        with self.assertRaises(elfaudit.ELFError):
            elfaudit.ELFImage(bytes(image))

        image = bytearray(synthetic_elf())
        struct.pack_into(">H", image, 16, 1)
        with self.assertRaises(elfaudit.ELFError):
            elfaudit.ELFImage(bytes(image))

        image = bytearray(synthetic_elf())
        struct.pack_into(">Q", image, 32, len(image) - 8)
        with self.assertRaises(elfaudit.ELFError):
            elfaudit.ELFImage(bytes(image))

    def test_rejects_unmapped_ranges_and_malformed_signature(self):
        image = elfaudit.ELFImage(synthetic_elf())
        with self.assertRaises(elfaudit.ELFError):
            image.read_virtual(0x20000, 4)
        with self.assertRaises(elfaudit.argparse.ArgumentTypeError):
            elfaudit.parse_hex("123")


if __name__ == "__main__":
    unittest.main()
