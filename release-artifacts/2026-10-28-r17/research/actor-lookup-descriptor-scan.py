#!/usr/bin/env python3
"""Narrow, read-only PPC scan for the BLES 01.24 actor-lookup descriptor.

This is a candidate-byte scan, not a disassembler or a proof of global xrefs.
See actor-lookup-descriptor-evidence.md for the Ghidra instruction review and
the limits of the scan.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from rtm_elf_audit import ELFError, ELFImage, PF_X  # noqa: E402

EXPECTED_SHA256 = "e7f100b38bbaf6c23f71b390fdb103d31b427187c38dcfbd47e09c94c4a44206"
ACTOR_LOOKUP_ADDRESS = 0x00255FA8
ACTOR_LOOKUP_SIGNATURE = bytes.fromhex(
    "7C0802A648B42A5DF821FF21F80100F0607E00003C6001A23FE001A23063E160"
    "33FF1138809E01743BA00001548404FE80A3000C7C8429D6806300087F432014"
)
DESCRIPTOR_BASE = 0x01A1E160
BITMAP_BASE = 0x01A21138
AI_POOL_STRIDE_CANDIDATE = bytes.fromhex("000028D0")

WINDOWS = (
    (0x00255FBc, 0x00256060),
    (0x0015CD04, 0x0015CD34),
    (0x0015CE1C, 0x0015CE34),
    (0x00193B14, 0x00193B50),
    (0x00193C28, 0x00193C48),
)

BRANCH_OPCODES = {16, 18, 19}
STORE_OPCODES = {36, 62}  # stw, std


def instruction_words(image: ELFImage):
    for segment in image.segments:
        if not segment.flags & PF_X:
            continue
        code = image.data[segment.file_offset : segment.file_offset + segment.file_size]
        aligned_length = len(code) - (len(code) % 4)
        for offset in range(0, aligned_length, 4):
            yield segment.virtual_address + offset, int.from_bytes(code[offset : offset + 4], "big")


def materialization_pairs(words, low_immediate: int):
    """Find `lis rD,0x1A2` then addic/addi rD,rD,low within four words."""
    pairs = []
    for index, (address, word) in enumerate(words):
        opcode = word >> 26
        destination = (word >> 21) & 0x1F
        base = (word >> 16) & 0x1F
        immediate = word & 0xFFFF
        if opcode != 15 or base != 0 or immediate != 0x1A2:  # lis rD,0x1A2
            continue
        for next_index in range(index + 1, min(index + 5, len(words))):
            next_address, next_word = words[next_index]
            next_opcode = next_word >> 26
            next_destination = (next_word >> 21) & 0x1F
            next_base = (next_word >> 16) & 0x1F
            next_immediate = next_word & 0xFFFF
            if (
                next_address == address + (next_index - index) * 4
                and next_opcode in (12, 14)  # addic/addi
                and next_destination == destination
                and next_base == destination
                and next_immediate == low_immediate
            ):
                pairs.append((address, next_address, destination, next_word))
                break
    return pairs


def simple_direct_field_stores(words, pairs):
    """Heuristically look for nearby stw/std to descriptor +8/+C.

    Stops at branches and simple writes to the materialized base register.
    It intentionally does not follow control flow, register aliases, or calls.
    """
    index_by_address = {address: index for index, (address, _) in enumerate(words)}
    writes = []
    for pair_address, add_address, base_register, _ in pairs:
        pair_index = index_by_address.get(pair_address)
        if pair_index is None:
            continue
        add_index = pair_index + ((add_address - pair_address) // 4)
        for index in range(add_index + 1, min(add_index + 41, len(words))):
            address, word = words[index]
            opcode = word >> 26
            destination = (word >> 21) & 0x1F
            base = (word >> 16) & 0x1F
            immediate = word & 0xFFFF

            if opcode in STORE_OPCODES and base == base_register and immediate in (8, 12):
                writes.append((pair_address, address, opcode, base_register, immediate, word))
                break
            if opcode in BRANCH_OPCODES:
                break
            # Arithmetic and load families use RT as their destination. This
            # conservative check is only a quick filter; not a complete PPC CFG.
            if opcode not in STORE_OPCODES and destination == base_register:
                break
    return writes


def print_window(image: ELFImage, start: int, end: int) -> None:
    print(f"\nPPC BE words 0x{start:08X}..0x{end:08X}:")
    for address in range(start, end, 4):
        try:
            word = image.read_virtual(address, 4)
        except ELFError as error:
            print(f"  0x{address:08X}: unmapped ({error})")
        else:
            print(f"  0x{address:08X}: {word.hex().upper()}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="decrypted BLES01857 01.24 EBOOT.ELF")
    args = parser.parse_args()

    try:
        data = args.elf.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != EXPECTED_SHA256:
            raise ValueError(f"ELF hash mismatch: expected {EXPECTED_SHA256}, got {digest}")
        image = ELFImage(data)
    except (OSError, ELFError, ValueError) as error:
        print(f"audit failed: {error}", file=sys.stderr)
        return 2

    observed = image.read_virtual(ACTOR_LOOKUP_ADDRESS, len(ACTOR_LOOKUP_SIGNATURE))
    print(f"ELF SHA-256: {digest}")
    print(f"Actor lookup signature at 0x{ACTOR_LOOKUP_ADDRESS:08X}: {observed.hex().upper()}")
    print(f"Signature match: {observed == ACTOR_LOOKUP_SIGNATURE}")

    words = list(instruction_words(image))
    descriptor_pairs = materialization_pairs(words, 0xE160)
    bitmap_pairs = materialization_pairs(words, 0x1138)
    direct_stores = simple_direct_field_stores(words, descriptor_pairs)
    stride_hits = image.find_aligned(AI_POOL_STRIDE_CANDIDATE)
    print(f"Aligned lis/addic candidate pairs for 0x{DESCRIPTOR_BASE:08X}: {len(descriptor_pairs)}")
    print(f"Simple nearby direct stw/std candidates to descriptor +8/+C: {len(direct_stores)}")
    print(f"Aligned executable 0x28D0 literals: {len(stride_hits)}")
    print(f"Aligned lis/addic candidate pairs for bitmap 0x{BITMAP_BASE:08X}: {len(bitmap_pairs)}")
    print("Counts are byte-pattern candidates in executable PT_LOAD segments, not complete xrefs.")
    print("The direct-store filter is linear and does not follow aliases, branches, or indirect calls.")

    for start, end in WINDOWS:
        print_window(image, start, end)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
