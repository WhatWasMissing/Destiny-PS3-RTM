from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path
from typing import Any

from generate_spawn_program import expand_frame, words_for_event


BASE = 0x183E000
MAX_BYTES = 0x400
SAVED_GPRS = [2, 25, 28, 29, 30, 31]


def raw(pc: str, word: int, asm: str) -> dict[str, Any]:
    return {"pc": pc, "op": "raw_word", "args": [f"0x{word:08x}"], "asm": asm, "kind": "word"}


def label(pc: str, name: str) -> dict[str, Any]:
    return {"pc": pc, "op": "label_mark", "args": [name], "kind": "label"}


def branch(pc: str, op: str, target: str) -> dict[str, Any]:
    return {"pc": pc, "op": op, "args": [target], "kind": "fixup"}


def event(pc: str, op: str, args: list[Any], kind: str = "word", asm: str | None = None) -> dict[str, Any]:
    value: dict[str, Any] = {"pc": pc, "op": op, "args": args, "kind": kind}
    if asm:
        value["asm"] = asm
    return value


# Ordered transcription of FUN_1402d0030. Plain words are taken from the
# emitter's final OR expressions; relocations retain the actual wrapper/tag.
EVENTS: list[dict[str, Any]] = [
    event("1402d00ba", "FUN_1402d20a0", [0x100, SAVED_GPRS], "words9", "stdu/mflr/save r2,r25,r28-r31/LR"),
    raw("1402d01a0", 0x3fe001c7, "addis r31,r0,0x1c7"),
    raw("1402d01a0", 0x3bff4000, "addi r31,r31,0x4000"),
    raw("1402d01a0", 0x801f0004, "lwz r0,4(r31)"),
    raw("1402d01a0", 0x2c000001, "cmpwi r0,1"),
    branch("1402d0246", "FUN_1402cdfc0", "out"),
    raw("1402d0250", 0x819f0008, "lwz r12,8(r31)"),
    raw("1402d0250", 0x398c0001, "addi r12,r12,1"),
    raw("1402d0250", 0x919f0008, "stw r12,8(r31)"),
    raw("1402d0250", 0x3bdf0010, "addi r30,r31,0x10"),
    raw("1402d0250", 0x3ba00008, "li r29,8"),
    label("1402d032d", "task"),
    raw("1402d033d", 0x819e0000, "lwz r12,0(r30)"),
    raw("1402d036c", 0x2c0c0000, "cmpwi r12,0"),
    event("1402d03c9", "FUN_1402cdec0", ["task_next"], "fixup", "beq task_next"),
    raw("1402d03fe", 0x3c400189, "lis r2,0x189"),
    raw("1402d03fe", 0x60421798, "ori r2,r2,0x1798"),
    raw("1402d03fe", 0x806c0004, "lwz r3,4(r12)"),
    raw("1402d03fe", 0x7d8903a6, "mtctr r12"),
    raw("1402d03fe", 0x4e800421, "bctrl"),
    label("1402d05b0", "task_next"),
    raw("1402d05b0", 0x3bde0008, "addi r30,r30,8"),
    raw("1402d05b0", 0x3bbdffff, "addi r29,r29,-1"),
    raw("1402d05b0", 0x2c1d0000, "cmpwi r29,0"),
    branch("1402d0582", "FUN_1402cdfc0", "task"),
    raw("1402d0590", 0x3bdf0100, "addi r30,r31,0x100"),
    raw("1402d0590", 0x3ba00010, "li r29,16"),
    label("1402d0a00", "slot"),
    raw("1402d0a00", 0x801e0000, "lwz r0,0(r30)"),
    raw("1402d0a00", 0x2c000001, "cmpwi r0,1"),
    branch("1402d0714", "FUN_1402cdfc0", "slot_next"),
    raw("1402d0720", 0x38000002, "addi r0,r0,2"),
    raw("1402d0720", 0x901e0000, "stw r0,0(r30)"),
    raw("1402d0720", 0xe85e0008, "ld r2,8(r30)"),
    raw("1402d0720", 0xc83e0050, "lfd f1,0x50(r30)"),
    raw("1402d0720", 0xc85e0058, "lfd f2,0x58(r30)"),
    raw("1402d0720", 0xc87e0060, "lfd f3,0x60(r30)"),
    raw("1402d0720", 0xc89e0068, "lfd f4,0x68(r30)"),
    raw("1402d0720", 0xe87e0010, "ld r3,0x10(r30)"),
    raw("1402d0720", 0xe89e0018, "ld r4,0x18(r30)"),
    raw("1402d0720", 0xe8be0020, "ld r5,0x20(r30)"),
    raw("1402d0720", 0xe8de0028, "ld r6,0x28(r30)"),
    raw("1402d0720", 0xe8fe0030, "ld r7,0x30(r30)"),
    raw("1402d0720", 0xe91e0038, "ld r8,0x38(r30)"),
    raw("1402d0720", 0xe93e0040, "ld r9,0x40(r30)"),
    raw("1402d0720", 0xe95e0048, "ld r10,0x48(r30)"),
    raw("1402d0720", 0x819e0004, "lwz r12,4(r30)"),
    raw("1402d0720", 0x7d8903a6, "mtctr r12"),
    raw("1402d0720", 0x4e800421, "bctrl"),
    raw("1402d0720", 0xf87e0070, "std r3,0x70(r30)"),
    raw("1402d0720", 0xd83e0078, "stfd f1,0x78(r30)"),
    raw("1402d0720", 0x7c0004ac, "sync"),
    raw("1402d0720", 0x38000003, "li r0,3"),
    raw("1402d0720", 0x901e0000, "stw r0,0(r30)"),
    label("1402d0b80", "slot_next"),
    raw("1402d0b80", 0x3bde0080, "addi r30,r30,0x80"),
    raw("1402d0b80", 0x3bbdffff, "addi r29,r29,-1"),
    raw("1402d0b80", 0x2c1d0000, "cmpwi r29,0"),
    branch("1402d0a54", "FUN_1402cdfc0", "slot"),
    label("1402d0afd", "out"),
    event("1402d0b00", "FUN_1402cff40", [0x100, SAVED_GPRS], "words9", "restore LR/r2/r25/r28-r31; addi r1,r1,0x100"),
    event("1402d0c48", "FUN_1402ce0c0", ["0x48000000", "0x3510f0"], "fixup", "b 0x3510f0"),
]


def decode_branch(word: int, pc: int, width: int) -> int | None:
    opcode = word >> 26
    if opcode == 16 and width == 16:
        encoded = word & 0xFFFC
        sign_bit = 1 << 15
        delta = encoded - (1 << 16) if encoded & sign_bit else encoded
        return pc + delta
    if opcode == 18 and width == 26:
        encoded = word & 0x03FFFFFC
        sign_bit = 1 << 25
        delta = encoded - (1 << 26) if encoded & sign_bit else encoded
        return pc + delta
    return None


def build(out_dir: Path) -> dict[str, Any]:
    words: list[int] = []
    labels: dict[str, int] = {}
    fixups: list[tuple[int, int, int, Any, str, str]] = []
    metadata: list[dict[str, Any]] = []

    for e in EVENTS:
        kind, op, args = e["kind"], e["op"], e["args"]
        if kind == "label":
            name = str(args[0])
            if name in labels:
                raise ValueError(f"duplicate label: {name}")
            labels[name] = BASE + len(words) * 4
            continue
        if kind == "fixup":
            if op == "FUN_1402cdec0":
                template, width, target = 0x41820000, 16, args[0]
            elif op == "FUN_1402cdfc0":
                template, width, target = 0x40820000, 16, args[0]
            elif op == "FUN_1402ce0c0":
                template, width, target = int(args[0], 0), 26, args[1]
            else:
                raise ValueError(f"unsupported bootstrap fixup wrapper {op}")
            site = len(words)
            words.append(0)
            fixups.append((site, template, width, target, op, str(e["pc"])))
            metadata.append({"pc": e["pc"], "op": op, "asm": e.get("asm", ""), "relocation": True})
            continue
        if kind == "words9":
            emitted = expand_frame(op, args)
        elif kind == "word":
            emitted = words_for_event(op, args)
        else:
            raise ValueError(f"unsupported event kind {kind}")
        if len(emitted) != (9 if kind == "words9" else 1):
            raise ValueError(f"unexpected emission count at {e['pc']}")
        for part, word in enumerate(emitted):
            words.append(word & 0xFFFFFFFF)
            metadata.append({"pc": e["pc"], "op": op, "asm": e.get("asm", ""), "part": part, "relocation": False})

    for site, template, width, target_value, op, pc_tag in fixups:
        if isinstance(target_value, str) and target_value in labels:
            target = labels[target_value]
        elif isinstance(target_value, str):
            target = int(target_value, 0)
        else:
            target = int(target_value)
        pc = BASE + site * 4
        delta = target - pc
        bound = 1 << (width - 1)
        if delta & 3 or not -bound <= delta < bound:
            raise ValueError(f"unreachable/unaligned {op} at {pc_tag}: delta={delta:#x}")
        # FUN_1402d1b20 uses (1 << width) - 4 for the stored displacement.
        word = (template | (delta & ((1 << width) - 4))) & 0xFFFFFFFF
        decoded = decode_branch(word, pc, width)
        if decoded != target:
            raise ValueError(f"fixup failed independent decode at {pc_tag}: {decoded!r} != {target:#x}")
        words[site] = word
        metadata[site].update(target=target_value, target_address=f"0x{target:08x}", delta=f"{delta:+#x}", width=width, word=f"0x{word:08x}")

    if labels.keys() != {"task", "task_next", "slot", "slot_next", "out"}:
        raise ValueError(f"unexpected labels: {sorted(labels)}")
    payload = b"".join(struct.pack(">I", word) for word in words)
    if len(payload) > MAX_BYTES:
        raise ValueError(f"bootstrap executor exceeds 0x400-byte limit: {len(payload):#x}")

    out_dir.mkdir(parents=True, exist_ok=True)
    events_path = out_dir / "bootstrap-executor-events.jsonl"
    events_path.write_text("".join(json.dumps(e, separators=(",", ":")) + "\n" for e in EVENTS), encoding="utf-8")
    bin_path = out_dir / "bootstrap-executor.bin"
    bin_path.write_bytes(payload)
    listing_path = out_dir / "bootstrap-executor-words.txt"
    listing_path.write_text("\n".join(
        f"{i:03d}  {BASE + i * 4:#010x}  0x{word:08x}  {metadata[i]['pc']}  {metadata[i]['asm'] or metadata[i]['op']}"
        + (f" -> {metadata[i]['target_address']} ({metadata[i]['delta']})" if metadata[i]["relocation"] else "")
        for i, word in enumerate(words)
    ) + "\n", encoding="utf-8")
    summary = {
        "source": "Blackrock FUN_1402d0030, called by FUN_1402d16d0",
        "base_address": f"0x{BASE:08x}",
        "continuation": "0x003510f0",
        "word_count": len(words),
        "byte_count": len(payload),
        "limit_bytes": MAX_BYTES,
        "labels": {name: f"0x{addr:08x}" for name, addr in labels.items()},
        "fixup_count": len(fixups),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "endianness": "big",
        "validation": "all fixups resolved, range-checked, and independently decoded back to their target addresses",
        "warning": "offline reconstruction only; not installed or sent to a console",
    }
    (out_dir / "bootstrap-executor-summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


if __name__ == "__main__":
    print(json.dumps(build(Path(__file__).resolve().parent), indent=2))
