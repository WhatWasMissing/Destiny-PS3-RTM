"""Execute the bootstrap's 32-bit-compatible task loop with Unicorn.

This checks task-entry addressing and callback register values only. It does
not emulate the PPC64 prologue, spawn worker, game engine, or instruction cache.
Run from the repository root with Python and the Unicorn package installed.
"""

from pathlib import Path
from struct import pack

try:
    from unicorn import Uc, UC_ARCH_PPC, UC_HOOK_CODE, UC_MODE_32, UC_MODE_BIG_ENDIAN
    from unicorn.ppc_const import UC_PPC_REG_2, UC_PPC_REG_3, UC_PPC_REG_12, UC_PPC_REG_30, UC_PPC_REG_31
except ImportError as exc:
    raise SystemExit("This optional offline check requires the Python unicorn package.") from exc


ROOT = Path(__file__).resolve().parent
BOOTSTRAP = ROOT / "bootstrap-executor.bin"
PROGRAM = ROOT / "spawn-program-first-slot.bin"
CODE_BASE = 0x0183E000
PROGRAM_BASE = 0x0183E400
TASKS_BASE = 0x01C74010
DATA_BASE = 0x01C75000
TASK_LOOP = 0x0183E044
COMMAND_LOOP = 0x0183E084
TOC = 0x01891798


def dispatch(bootstrap: bytes, program: bytes, slot: int | None) -> list[tuple[int, int, int]]:
    cpu = Uc(UC_ARCH_PPC, UC_MODE_32 | UC_MODE_BIG_ENDIAN)
    cpu.mem_map(CODE_BASE, 0x2000)
    cpu.mem_map(0x01C74000, 0x2000)
    cpu.mem_write(CODE_BASE, bootstrap)
    cpu.mem_write(PROGRAM_BASE, program)
    if slot is not None:
        cpu.mem_write(TASKS_BASE + 8 * slot, pack(">II", PROGRAM_BASE, DATA_BASE + 0x40 * slot))
    cpu.reg_write(UC_PPC_REG_31, 0x01C74000)

    callbacks: list[tuple[int, int, int]] = []

    def on_instruction(uc: Uc, address: int, _size: int, _data: object) -> None:
        if address == PROGRAM_BASE:
            callbacks.append((uc.reg_read(UC_PPC_REG_3), uc.reg_read(UC_PPC_REG_12), uc.reg_read(UC_PPC_REG_2)))
            uc.emu_stop()

    cpu.hook_add(UC_HOOK_CODE, on_instruction)
    cpu.emu_start(TASK_LOOP, COMMAND_LOOP, count=1000)
    return callbacks


def command_header(bootstrap: bytes) -> tuple[int, int]:
    cpu = Uc(UC_ARCH_PPC, UC_MODE_32 | UC_MODE_BIG_ENDIAN)
    cpu.mem_map(CODE_BASE, 0x2000)
    cpu.mem_map(0x01C74000, 0x1000)
    cpu.mem_write(CODE_BASE, bootstrap)
    slot = 0x01C74100
    cpu.mem_write(slot, pack(">I", 1))
    cpu.mem_write(slot + 8, pack(">II", TOC, 0xDEADBEEF))
    cpu.reg_write(UC_PPC_REG_30, slot)
    # Stop before the following PPC64 argument loads and callback.
    cpu.emu_start(0x0183E084, 0x0183E09C, count=20)
    return int.from_bytes(cpu.mem_read(slot, 4), "big"), cpu.reg_read(UC_PPC_REG_2)


def main() -> None:
    bootstrap = BOOTSTRAP.read_bytes()
    program = PROGRAM.read_bytes()
    if len(bootstrap) != 292 or len(program) != 2908:
        raise SystemExit("Unexpected executor image lengths")
    if bootstrap[0x60:0x64] != bytes.fromhex("807E0004"):
        raise SystemExit("Task context instruction does not load from the task entry")

    assert dispatch(bootstrap, program, None) == [], "empty task table called a worker"
    for slot in (0, 3, 7):
        expected = [(DATA_BASE + 0x40 * slot, PROGRAM_BASE, TOC)]
        observed = dispatch(bootstrap, program, slot)
        assert observed == expected, f"slot {slot}: expected {expected!r}; got {observed!r}"
    assert command_header(bootstrap) == (2, TOC), "command header did not mark status 2 or load its 32-bit TOC"

    previous = bytearray(bootstrap)
    previous[0x60:0x64] = bytes.fromhex("806C0004")
    old_observed = dispatch(bytes(previous), program, 0)
    expected_bad_context = int.from_bytes(program[4:8], "big")
    assert old_observed == [(expected_bad_context, PROGRAM_BASE, TOC)]
    assert expected_bad_context != DATA_BASE

    print("PASS: empty table and task slots 0, 3, 7 dispatch the expected context, callback and TOC.")
    print("PASS: generic command header changes status 1 to 2 and loads the 32-bit TOC.")
    print(f"Regression reproduced: previous instruction passed callback word 0x{expected_bad_context:08X} as context.")
    print("Scope: 32-bit-compatible dispatch loop only; no PPC64 worker or PS3 execution.")


if __name__ == "__main__":
    main()
