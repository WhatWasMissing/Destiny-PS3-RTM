#!/usr/bin/env python3
"""Read-only static audit for the recovered BLES01857 01.24 Destiny EBOOT."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


EXPECTED_BLES01857_0124_SHA256 = "e7f100b38bbaf6c23f71b390fdb103d31b427187c38dcfbd47e09c94c4a44206"
PT_LOAD = 1
PF_X = 1
ET_EXEC = 2
EM_PPC64 = 21
MAX_U64 = (1 << 64) - 1

KNOWN_SITES = (
    ("Unlimited Ammo - primary OFF", 0x003108EC, "63C40000"),
    ("Unlimited Ammo - secondary OFF", 0x00310554, "63C50000"),
    ("Unlimited Ammo - heavy OFF", 0x00CF95AC, "63DD0000408200083BA00001807F001CD3DF0110"),
    ("Former app ammo target - claimed OFF", 0x00310F94, "3F800000"),
    ("Brightness - Normal", 0x006101C0, "3F800000"),
    ("FOV default", 0x0070906C, "40000000"),
    ("Flip View - OFF", 0x00614C40, "3F000000"),
    ("Power Boost - OFF", 0x0005E614, "3F800000"),
    ("One-Hit - OFF halfword", 0x0043E626, "3F80"),
    ("Warpriest HTML - source OFF instruction", 0x00444CA4, "C0434DB0"),
    ("Warpriest HTML - source OFF data", 0x01897420, "3F800000"),
    ("Warpriest Overwalls CSharp - source OFF instruction", 0x00444CA4, "C04100F0"),
    ("Warpriest Overwalls CSharp - secondary BSS initial value", 0x01891AE0, "00000000"),
    ("Deathsinger - reset fallback signature", 0x001C7B58, "38C6FFFF"),
    ("Overwalls plate hook - source branch-site pair", 0x00A015AC, "3063FFF44BFFFB15"),
    ("Overwalls plate hook - code-cave initial word", 0x010FB514, "00000000"),
    ("Overwalls plate hook - state-flag initial BSS", 0x0190EC98, "00000000"),
    ("Overwalls physics hook - source branch instruction", 0x001C4110, "30640140"),
    ("Overwalls physics hook - code-cave initial word", 0x010D8EC0, "00000000"),
    ("Player Size - object-handle resolver signature", 0x002FF2D0, "7C0802A648A99751F821FF81F801009038A0FFFF607F00002C04FFFF418200483C6001A2548404FE30C31CB08066000C7C8419D680E600083061007080C600247C87201490A100707885002080A500087CA530387C852010788400204BEFC09D80A1007090BF00003821008048A9974C"),
    ("Player Size - resolved-index getter signature", 0x001FB3C8, "8084001C908300004E800020"),
    ("Player Size - runtime lookup table initial BSS", 0x01A21CB0, "00000000"),
    ("Player Size - runtime object base initial BSS", 0x01A1D9E8, "00000000"),
)


@dataclass(frozen=True)
class LoadSegment:
    file_offset: int
    virtual_address: int
    file_size: int
    memory_size: int
    flags: int


class ELFError(ValueError):
    pass


class ELFImage:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 64 or data[:4] != b"\x7fELF":
            raise ELFError("not a complete ELF64 file")
        if data[4] != 2 or data[5] != 2:
            raise ELFError("expected ELF64 big-endian input")
        (
            self.elf_type,
            self.machine,
            version,
            self.entry_point,
            phoff,
            _shoff,
            _flags,
            ehsize,
            phentsize,
            phnum,
            _shentsize,
            _shnum,
            _shstrndx,
        ) = struct.unpack_from(">HHIQQQIHHHHHH", data, 16)
        if version != 1 or ehsize < 64 or phentsize < 56:
            raise ELFError("unsupported or malformed ELF header")
        if self.elf_type != ET_EXEC or self.machine != EM_PPC64:
            raise ELFError("expected an executable PowerPC64 ELF")
        if phoff > len(data) or phnum > (len(data) - phoff) // phentsize:
            raise ELFError("program-header table extends beyond the file")

        segments = []
        for index in range(phnum):
            offset = phoff + index * phentsize
            p_type, p_flags, p_offset, p_vaddr, _paddr, p_filesz, p_memsz, _align = struct.unpack_from(
                ">IIQQQQQQ", data, offset
            )
            if p_type != PT_LOAD:
                continue
            if p_filesz > p_memsz or p_offset > len(data) or p_filesz > len(data) - p_offset:
                raise ELFError(f"PT_LOAD segment {index} has invalid file or memory bounds")
            if p_vaddr > MAX_U64 or p_memsz > MAX_U64 - p_vaddr:
                raise ELFError(f"PT_LOAD segment {index} overflows the virtual address space")
            segments.append(LoadSegment(p_offset, p_vaddr, p_filesz, p_memsz, p_flags))
        if not segments:
            raise ELFError("ELF contains no PT_LOAD segments")
        self.segments = tuple(segments)

    def read_virtual(self, address: int, length: int) -> bytes:
        if address < 0 or length < 1 or address > MAX_U64 or length - 1 > MAX_U64 - address:
            raise ELFError("invalid virtual address range")
        end = address + length
        covering = [
            segment
            for segment in self.segments
            if address >= segment.virtual_address
            and end <= segment.virtual_address + segment.memory_size
        ]
        if len(covering) != 1:
            raise ELFError(f"virtual range 0x{address:X}..0x{end - 1:X} is not uniquely mapped")
        segment = covering[0]
        relative = address - segment.virtual_address
        file_length = max(0, min(length, segment.file_size - relative))
        content = self.data[segment.file_offset + relative : segment.file_offset + relative + file_length]
        return content + bytes(length - file_length)

    def find_aligned(self, signature: bytes) -> list[int]:
        if not signature:
            raise ELFError("signature cannot be empty")
        hits = []
        for segment in self.segments:
            if not segment.flags & PF_X or segment.file_size < len(signature):
                continue
            content = self.data[segment.file_offset : segment.file_offset + segment.file_size]
            start = 0
            while True:
                offset = content.find(signature, start)
                if offset < 0:
                    break
                address = segment.virtual_address + offset
                if address % 4 == 0:
                    hits.append(address)
                start = offset + 1
        return sorted(set(hits))


def parse_hex(value: str) -> bytes:
    clean = value.strip().removeprefix("0x").removeprefix("0X")
    if not clean or len(clean) % 2 or not re.fullmatch(r"[0-9a-fA-F]+", clean):
        raise argparse.ArgumentTypeError("expected a non-empty whole-byte hexadecimal signature")
    return bytes.fromhex(clean)


def audit(path: Path, signatures: list[bytes]) -> dict:
    data = path.read_bytes()
    image = ELFImage(data)
    digest = hashlib.sha256(data).hexdigest()
    sites = []
    for name, address, expected_hex in KNOWN_SITES:
        try:
            observed = image.read_virtual(address, len(expected_hex) // 2).hex().upper()
        except ELFError as error:
            observed = None
            mapping_error = str(error)
        else:
            mapping_error = None
        sites.append(
            {
                "name": name,
                "address": f"0x{address:08X}",
                "expected": expected_hex,
                "observed": observed,
                "matches": observed == expected_hex,
                "mapping_error": mapping_error,
            }
        )
    return {
        "file": str(path.resolve()),
        "size": len(data),
        "sha256": digest,
        "is_expected_bles01857_0124": digest == EXPECTED_BLES01857_0124_SHA256,
        "elf_type": image.elf_type,
        "machine": image.machine,
        "entry_point": f"0x{image.entry_point:016X}",
        "load_segments": [asdict(segment) for segment in image.segments],
        "known_sites": sites,
        "signatures": [
            {
                "hex": signature.hex().upper(),
                "aligned_executable_addresses": [f"0x{address:08X}" for address in image.find_aligned(signature)],
            }
            for signature in signatures
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="decrypted Destiny ELF/EBOOT file to inspect")
    parser.add_argument(
        "--search-signature",
        action="append",
        default=[],
        metavar="HEX",
        help="find aligned occurrences in executable PT_LOAD segments (repeatable)",
    )
    parser.add_argument("--json", action="store_true", help="write machine-readable JSON")
    args = parser.parse_args(argv)
    try:
        signatures = [parse_hex(value) for value in args.search_signature]
        result = audit(args.elf, signatures)
    except (OSError, ELFError, argparse.ArgumentTypeError) as error:
        print(f"ELF audit failed: {error}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result, indent=2))
        return 0

    build = "BLES01857 01.24" if result["is_expected_bles01857_0124"] else "unknown build (not the verified BLES01857 01.24 hash)"
    print(f"Build: {build}")
    print(f"File: {result['file']}")
    print(f"SHA-256: {result['sha256']}")
    print(f"ELF type={result['elf_type']} machine={result['machine']} entry={result['entry_point']}")
    print("Known source sites (a byte match does not prove runtime effect):")
    for site in result["known_sites"]:
        observed = site["observed"] or f"unmapped: {site['mapping_error']}"
        print(f"  {site['address']} {site['name']}: expected {site['expected']}, observed {observed}, match={site['matches']}")
    for signature in result["signatures"]:
        addresses = ", ".join(signature["aligned_executable_addresses"]) or "none"
        print(f"Signature {signature['hex']}: {addresses}")
    if not result["is_expected_bles01857_0124"]:
        print("Address results are only comparisons; they are not compatibility evidence for this unidentified build.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
