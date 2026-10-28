from __future__ import annotations

import argparse
import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_BASE = 0x183E000
MAX_PROGRAM_BYTES = 0xC00


def u32(value: int) -> int:
    return value & 0xFFFFFFFF


def reg(value: Any) -> int:
    number = int(value)
    if not 0 <= number <= 31:
        raise ValueError(f"register out of range: {number}")
    return number


def simm16(value: Any) -> int:
    number = int(value)
    if not -0x8000 <= number <= 0x7FFF:
        raise ValueError(f"signed 16-bit immediate out of range: {number}")
    return number & 0xFFFF


def uimm16(value: Any) -> int:
    number = int(value)
    if not 0 <= number <= 0xFFFF:
        raise ValueError(f"unsigned 16-bit immediate out of range: {number}")
    return number


def dsdisp(value: Any) -> int:
    number = int(value)
    if number & 3:
        raise ValueError(f"DS-form displacement is not 4-byte aligned: {number}")
    return simm16(number)


def dform(base: int, rt: Any, disp: Any, ra: Any) -> int:
    return u32(base | (reg(rt) << 21) | (reg(ra) << 16) | simm16(disp))


def xform(base: int, rd: Any, ra: Any, rb: Any) -> int:
    return u32(base | (reg(rd) << 21) | (reg(ra) << 16) | (reg(rb) << 11))


@dataclass
class Fixup:
    site: int
    template: int
    width: int
    target: Any
    line: int
    op: str


def target_int(value: Any) -> int:
    if isinstance(value, bool):
        raise ValueError("boolean cannot be a relocation target")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"unsupported direct relocation target: {value!r}")


def words_for_event(op: str, args: list[Any]) -> list[int]:
    if op == "raw_word":
        return [u32(int(str(args[0]), 0))]
    if op == "FUN_1402d4c00":
        return [u32(int(str(args[0]), 0))]
    if op == "FUN_1402fa0e0":
        return [0x4E800020]
    if op == "FUN_1403cb950":
        return [0x4D800020]
    if op == "FUN_1403cf2c0":
        return [0x7C2004AC]
    if op == "FUN_1404357e0":
        return [0x4C800020]

    if op == "FUN_1402cc150":
        rt, ra, imm = args
        if int(ra) == 0:
            raise ValueError("Blackrock rejects addi with rA=r0; this event is invalid")
        return [u32(0x38000000 | (reg(rt) << 21) | (reg(ra) << 16) | simm16(imm))]
    if op == "FUN_1402cfd50":
        ra, imm = args
        return [u32(0x2C000000 | (reg(ra) << 16) | simm16(imm))]
    if op == "FUN_1402d1ad0":
        rt, imm = args
        return [u32(0x38000000 | (reg(rt) << 21) | simm16(imm))]
    if op == "FUN_1402d1e70":
        rt, imm = args
        return [u32(0x3C000000 | (reg(rt) << 21) | uimm16(imm))]
    if op == "FUN_1402d1ec0":
        rt, value = args
        rt = reg(rt)
        value = int(value)
        if not 0 <= value <= 0xFFFFFFFF:
            raise ValueError(f"lis/ori literal out of 32-bit range: {value}")
        high = (value >> 16) & 0xFFFF
        low = value & 0xFFFF
        return [
            u32(0x3C000000 | (rt << 21) | high),
            u32(0x60000000 | (rt << 21) | (rt << 16) | low),
        ]
    if op == "FUN_1402d2040":
        rs, ra, imm = args
        return [u32(0x60000000 | (reg(rs) << 21) | (reg(ra) << 16) | uimm16(imm))]
    if op == "FUN_140435750":
        imm = simm16(args[0])
        return [u32(0x38600000 | imm)]
    if op == "FUN_1402d1fb0":
        return [u32(0x7C0802A6 | (reg(args[0]) << 21))]
    if op == "FUN_1402d1fe0":
        return [u32(0x7C0903A6 | (reg(args[0]) << 21))]
    if op == "FUN_1402d2010":
        return [u32(0x7C0803A6 | (reg(args[0]) << 21))]

    if op == "FUN_1402d1f50":
        return [dform(0x80000000, *args)]
    if op == "FUN_1402d45d0":
        return [dform(0x90000000, *args)]
    if op == "FUN_1402d1a70":
        return [dform(0xC8000000, *args)]
    if op == "FUN_1403d9ca0":
        return [dform(0xC0000000, *args)]
    if op == "FUN_1403d9d20":
        return [dform(0x84000000, *args)]
    if op == "FUN_1403db950":
        return [dform(0xD0000000, *args)]
    if op == "FUN_140383960":
        return [dform(0x88000000, *args)]
    if op == "FUN_140380030":
        return [dform(0xB0000000, *args)]
    if op == "FUN_14037ffb0":
        return [dform(0x98000000, *args)]
    if op == "FUN_1403ba4a0":
        return [dform(0xA0000000, *args)]
    if op == "FUN_14043cee0":
        return [dform(0x94000000, *args)]
    if op == "FUN_1402d4540":
        rt, disp, ra = args
        return [u32(0xF8000000 | (reg(rt) << 21) | (reg(ra) << 16) | dsdisp(disp))]

    if op == "FUN_1404356c0":
        rt, value = args
        rt = reg(rt)
        value = int(value)
        if not 0 <= value <= 0xFFFFFFFF:
            raise ValueError(f"address literal out of range: {value}")
        high_adjusted = ((value >> 16) + ((value >> 15) & 1)) & 0xFFFF
        low_signed = value & 0xFFFF
        if low_signed & 0x8000:
            low_signed -= 0x10000
        return [
            u32(0x3C000000 | (rt << 21) | high_adjusted),
            dform(0x80000000, rt, low_signed, rt),
        ]
    if op == "FUN_140435410":
        mode, fpr, disp, ra = args
        mode = int(mode)
        if mode not in (0, 1):
            raise ValueError(f"unknown FUN_140435410 load mode: {mode}")
        base = 0xC0000000 if mode == 0 else 0xD0000000
        fpr = reg(fpr)
        ra = reg(ra)
        disp = int(disp)
        return [dform(base, fpr + i, disp + (4 * i), ra) for i in range(3)]

    if op == "FUN_14030dc40":
        ra, rs = args
        ra, rs = reg(ra), reg(rs)
        return [u32(0x7C000378 | (rs << 21) | (ra << 16) | (rs << 11))]
    if op == "FUN_14035d820":
        crfd, ra = args
        return [u32(0x7C000040 | (reg(crfd) << 16) | (reg(ra) << 11))]
    if op == "FUN_1403618d0":
        rd, ra, rb = map(reg, args)
        return [u32(0x7C000214 | (rd << 21) | (ra << 16) | (rb << 11))]
    if op == "FUN_140362de0":
        if len(args) != 5:
            raise ValueError(f"FUN_140362de0 requires five operands, got {args}")
        p2, p3, p4, p5, p6 = map(int, args)
        value = (((((p3 | 0x2A0) << 5 | p2) << 5 | p4) << 5 | p5) << 5 | p6) << 1
        return [u32(value)]
    if op == "FUN_14037d810":
        return [xform(0x7C000028, *args)]
    if op == "FUN_1403800b0":
        return [xform(0x7C00012D, *args)]
    if op == "FUN_1403832b0":
        ra, imm = args
        return [u32(0x28000000 | (reg(ra) << 16) | uimm16(imm))]
    if op == "FUN_1403855f0":
        rt, ra, rb = map(int, args)
        value = ((((rt | ((ra | 0x2A0) << 5)) << 5 | rb) << 11) | (0x3E - 2 * rb))
        return [u32(value)]
    if op == "FUN_140385b00":
        rd, ra, rb = map(reg, args)
        return [u32(0x7C000050 | (rd << 21) | (ra << 16) | (rb << 11))]
    if op == "FUN_1403b84d0":
        return [xform(0x7C000039, *args)]
    if op == "FUN_1403c0000":
        p2, p3, p4 = map(int, args)
        value = (((p2 | (p3 << 5)) << 10 | p4) << 6) | 0x5400003E | ((-0x800 * p4 + 0x10000) & 0xFFFFFFFF)
        return [u32(value)]
    if op == "FUN_1403c71a0":
        p2, p3, imm = args
        imm = simm16(imm)
        return [u32(imm | (((reg(p2) | 0xE0) << 5 | reg(p3)) << 16))]
    if op == "FUN_1403cb7b0":
        p2, p3, p4 = map(reg, args)
        return [u32(0x7C000038 | (p3 << 21) | (p2 << 16) | (p4 << 11))]
    if op == "FUN_1403cf310":
        return [xform(0x7C0001D6, *args)]
    if op == "FUN_140435900":
        return [xform(0x7C000396, *args)]

    if op == "FUN_140437480":
        p2, p3 = map(reg, args)
        return [u32(0xFC000210 | ((p2 << 10 | p3) << 11))]
    if op == "FUN_1404374d0":
        p2, p3, p4 = map(reg, args)
        return [u32(0xEC00002A | (((p2 << 5 | p3) << 5 | p4) << 11))]
    if op == "FUN_140437530":
        p2, p3 = map(reg, args)
        return [u32(0xFC00069C | ((p2 << 10 | p3) << 11))]
    if op == "FUN_140437580":
        p2, p3, p4 = map(int, args)
        value = (((((p2 | 0xFFFFFFF8) << 7) | p3) << 5) | p4) << 11
        return [u32(value)]
    if op == "FUN_1404375e0":
        p2, p3, p4 = map(reg, args)
        return [u32(0xEC000024 | (((p2 << 5 | p3) << 5 | p4) << 11))]
    if op == "FUN_1404376d0":
        if len(args) != 4:
            raise ValueError(f"FUN_1404376d0 requires four operands, got {args}")
        p2, p3, p4, p5 = map(reg, args)
        return [u32(((((p2 << 5 | p3) << 5 | p5) << 5 | p4) << 6) | 0xEC00003A)]
    if op == "FUN_140437730":
        p2, p3 = map(reg, args)
        return [u32(0xFC000090 | ((p2 << 10 | p3) << 11))]
    if op == "FUN_140437780":
        p2, p3, p4 = map(reg, args)
        return [u32(0xEC000032 | (((p2 << 5 | p3) << 10 | p4) << 6))]
    if op == "FUN_1404377e0":
        p2, p3 = map(reg, args)
        return [u32(0xFC000050 | ((p2 << 10 | p3) << 11))]
    if op == "FUN_140437830":
        p2, p3 = map(reg, args)
        return [u32(0xFC000018 | ((p2 << 10 | p3) << 11))]
    if op == "FUN_140437880":
        p2, p3 = map(reg, args)
        return [u32(0xEC00002C | ((p2 << 10 | p3) << 11))]
    if op == "FUN_1404378d0":
        p2, p3, p4 = map(reg, args)
        return [u32(0xEC000028 | (((p2 << 5 | p3) << 5 | p4) << 11))]

    if op == "FUN_14043cee0":
        return [dform(0x94000000, *args)]

    raise ValueError(f"no encoder for emitted operation {op!r}")


def fixup_for_event(event: dict[str, Any]) -> tuple[int, int, Any]:
    op = event["op"]
    args = event.get("args", [])
    if op == "FUN_1402cdec0":
        return 0x41820000, 16, args[0]
    if op == "FUN_1402cdfc0":
        return 0x40820000, 16, args[0]
    if op == "FUN_1402cfda0":
        return int(args[0]), 16, args[1]
    if op == "FUN_14030b430":
        return 0x48000000, 26, args[0]
    if op == "FUN_14030b530":
        return 0x48000001, 26, args[0]
    if op == "FUN_140382b00":
        return 0x40800000, 16, args[0]
    if op == "FUN_1403c4c90":
        return 0x41810000, 16, args[0]
    if op == "FUN_1403c4d90":
        return 0x40810000, 16, args[0]
    if op == "FUN_1403c4e90":
        return 0x41800000, 16, args[0]
    if op == "FUN_1403cb810":
        return 0x48000001, 26, args[0]
    if op == "FUN_1403d8060":
        return 0x42000000, 16, args[0]
    if op == "FUN_1402ce0c0":
        return int(args[0]), 26, args[1]
    raise ValueError(f"no fixup encoding for {op!r}")


def expand_frame(op: str, args: list[Any]) -> list[int]:
    frame, saved = int(args[0]), [reg(x) for x in args[1]]
    if frame & 3:
        raise ValueError(f"frame size is not 4-byte aligned: {frame}")
    if op == "FUN_1402d20a0":
        words = [u32(0xF8210001 | ((-frame) & 0xFFFF)), 0x7C0802A6]
        for i, r in enumerate(saved):
            words.append(u32(0xF8010000 | (r << 21) | (1 << 16) | (0x70 + 8 * i)))
        words.append(u32(0xF8000000 | (0 << 21) | (1 << 16) | (0x70 + 8 * len(saved))))
        return words
    if op == "FUN_1402cff40":
        words = [u32(0xE8000000 | (0 << 21) | (1 << 16) | (0x70 + 8 * len(saved))), 0x7C0803A6]
        for i, r in enumerate(saved):
            words.append(u32(0xE8000000 | (r << 21) | (1 << 16) | (0x70 + 8 * i)))
        words.append(u32(0x38210000 | simm16(frame)))
        return words
    raise ValueError(f"unknown frame helper {op!r}")


def event_target(event: dict[str, Any]) -> Any:
    op = event["op"]
    args = event.get("args", [])
    if op in ("FUN_1402cfda0", "FUN_1402ce0c0"):
        return args[1]
    return args[-1]


def parse_direct_target(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str) and value.startswith(("0x", "0X")):
        return int(value, 16)
    return None


def build(events_path: Path, output_path: Path, summary_path: Path, listing_path: Path, base: int) -> dict[str, Any]:
    events = [json.loads(line) for line in events_path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    if not events or events[0].get("line") != 574 or events[-1].get("line") != 1762:
        raise ValueError("candidate must span source lines 574 through finalizer 1762")
    if sum(e.get("kind") == "finalizer" for e in events) != 1:
        raise ValueError("candidate must contain exactly one finalizer record")

    words: list[int] = []
    metadata: list[dict[str, Any]] = []
    labels: dict[str, int] = {}
    fixups: list[Fixup] = []

    for event in events:
        line = int(event.get("line", -1))
        op = str(event.get("op", ""))
        args = event.get("args", [])
        kind = event.get("kind")
        if kind == "label":
            name = str(args[0])
            if name in labels:
                raise ValueError(f"duplicate label {name!r} at line {line}")
            labels[name] = base + len(words) * 4
            continue
        if kind == "finalizer":
            continue
        if kind == "fixup":
            template, width, target = fixup_for_event(event)
            if width not in (16, 26):
                raise ValueError(f"unsupported relocation width {width} at line {line}")
            fixups.append(Fixup(len(words), u32(template), width, target, line, op))
            words.append(0)
            metadata.append({"line": line, "op": op, "part": 0, "relocation": True})
            continue
        if kind == "words9":
            emitted = expand_frame(op, args)
        else:
            emitted = words_for_event(op, args)
        expected = {"word": 1, "words2": 2, "words3": 3, "words9": 9}.get(kind)
        if expected is None or len(emitted) != expected:
            raise ValueError(f"line {line}: kind {kind!r} expected {expected} words, encoder emitted {len(emitted)}")
        for part, word in enumerate(emitted):
            words.append(u32(word))
            metadata.append({"line": line, "op": op, "part": part, "relocation": False})

    if len(words) * 4 > MAX_PROGRAM_BYTES:
        raise ValueError(f"program size {len(words) * 4:#x} exceeds Blackrock limit {MAX_PROGRAM_BYTES:#x}")

    for fixup in fixups:
        target_value = fixup.target
        if isinstance(target_value, str) and target_value in labels:
            target = labels[target_value]
        else:
            target = parse_direct_target(target_value)
            if target is None:
                raise ValueError(f"unresolved fixup target {target_value!r} at source line {fixup.line}")
        site_address = base + fixup.site * 4
        delta = target - site_address
        if delta & 3:
            raise ValueError(f"unaligned relocation at line {fixup.line}: delta {delta:#x}")
        bound = 1 << (fixup.width - 1)
        if not -bound <= delta < bound:
            raise ValueError(f"relocation out of signed {fixup.width}-bit range at line {fixup.line}: {delta:#x}")
        mask = (1 << fixup.width) - 4
        words[fixup.site] = u32(fixup.template | (delta & mask))
        metadata[fixup.site]["target"] = target_value
        metadata[fixup.site]["delta"] = delta
        metadata[fixup.site]["width"] = fixup.width
        metadata[fixup.site]["resolved_word"] = f"0x{words[fixup.site]:08x}"

    payload = b"".join(struct.pack(">I", word) for word in words)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)

    sha = hashlib.sha256(payload).hexdigest()
    direct_fixups = [
        {
            "line": f.line,
            "op": f.op,
            "site_word": f.site,
            "target": f.target,
            "delta": metadata[f.site]["delta"],
            "width": f.width,
            "word": metadata[f.site]["resolved_word"],
        }
        for f in fixups
        if parse_direct_target(f.target) is not None
    ]
    listing = [
        f"{i:04d}  +0x{i * 4:04x}  0x{word:08x}  line={metadata[i]['line']}  {metadata[i]['op']}"
        + (f"  target={metadata[i]['target']} delta={metadata[i]['delta']:+#x}" if metadata[i].get("relocation") else "")
        for i, word in enumerate(words)
    ]
    listing_path.write_text("\n".join(listing) + "\n", encoding="utf-8")

    summary = {
        "source_events": str(events_path),
        "base_address": f"0x{base:x}",
        "word_count": len(words),
        "byte_count": len(payload),
        "max_program_bytes": MAX_PROGRAM_BYTES,
        "fixup_count": len(fixups),
        "local_label_count": len(labels),
        "sha256": sha,
        "endianness": "big",
        "direct_target_fixups": direct_fixups,
    }
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def main() -> None:
    here = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Offline Blackrock op-1 PPU emitter reconstruction")
    parser.add_argument("--events", type=Path, default=here / "spawn-program-candidate.jsonl")
    parser.add_argument("--out", type=Path, default=here / "spawn-program.bin")
    parser.add_argument("--summary", type=Path, default=here / "spawn-program-summary.json")
    parser.add_argument("--listing", type=Path, default=here / "spawn-program-words.txt")
    parser.add_argument("--base-address", type=lambda s: int(s, 0), default=DEFAULT_BASE)
    args = parser.parse_args()
    summary = build(args.events, args.out, args.summary, args.listing, args.base_address)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
