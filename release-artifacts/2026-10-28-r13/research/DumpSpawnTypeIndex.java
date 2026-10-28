import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;

public class DumpSpawnTypeIndex extends GhidraScript {
  private static final long TYPE_TABLE = 0x0189A0CCL;
  private static final int TYPE_COUNT = 25;
  private static final int TYPE_RECORD_STRIDE = 0x1C;
  private static final long CONSTRUCTOR = 0x0013ED38L;
  private static final long CONSTRUCTOR_WINDOW_END = 0x0013EE40L;

  @Override
  protected void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length != 1) throw new IllegalArgumentException("Expected one output path");

    Memory memory = currentProgram.getMemory();
    try (BufferedWriter out = Files.newBufferedWriter(Paths.get(args[0]), StandardCharsets.UTF_8)) {
      out.write("Program: " + currentProgram.getName() + "\n");
      out.write("Type-name records at " + toAddr(TYPE_TABLE) + " (" + TYPE_COUNT + " × 0x" +
          Integer.toHexString(TYPE_RECORD_STRIDE) + ")\n");
      for (int i = 0; i < TYPE_COUNT; i++) {
        Address record = toAddr(TYPE_TABLE + (long) i * TYPE_RECORD_STRIDE);
        long rawName = Integer.toUnsignedLong(memory.getInt(record));
        Address name = toAddr(rawName);
        String text = readCString(memory, name);
        out.write(String.format("%02d record=%s namePtr=%s text=%s%n", i, record, name, text));
        dumpReferences(out, record, "record");
        dumpReferences(out, name, "name");
      }

      out.write("\nConstructor listing " + toAddr(CONSTRUCTOR) + " .. " +
          toAddr(CONSTRUCTOR_WINDOW_END) + "\n");
      Address address = toAddr(CONSTRUCTOR);
      while (address.compareTo(toAddr(CONSTRUCTOR_WINDOW_END)) < 0) {
        CodeUnit unit = currentProgram.getListing().getCodeUnitContaining(address);
        if (unit == null) {
          address = address.add(4);
          continue;
        }
        out.write(unit.getAddress() + "  " + unit.toString() + "\n");
        address = unit.getMaxAddress().add(1);
      }

      Function function = currentProgram.getFunctionManager().getFunctionAt(toAddr(CONSTRUCTOR));
      if (function == null) function = currentProgram.getFunctionManager().getFunctionContaining(toAddr(CONSTRUCTOR));
      out.write("\nConstructor function: " + (function == null ? "<not found>" : function.getName() + " " + function.getEntryPoint()) + "\n");
      if (function != null) {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try {
          DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
          if (result != null && result.decompileCompleted() && result.getDecompiledFunction() != null) {
            out.write(result.getDecompiledFunction().getC());
          } else {
            out.write("DECOMPILE FAILED: " + (result == null ? "null result" : result.getErrorMessage()) + "\n");
          }
        } finally {
          decompiler.dispose();
        }
      }
    }
  }

  private void dumpReferences(BufferedWriter out, Address target, String kind) throws Exception {
    ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
    int count = 0;
    while (references.hasNext()) count++;
    out.write("  " + kind + " xrefs=" + count);
    references = currentProgram.getReferenceManager().getReferencesTo(target);
    while (references.hasNext()) {
      Reference reference = references.next();
      out.write(" " + reference.getFromAddress() + "/" + reference.getReferenceType());
    }
    out.write("\n");
  }

  private String readCString(Memory memory, Address address) throws Exception {
    StringBuilder text = new StringBuilder();
    for (int i = 0; i < 100; i++) {
      int value = memory.getByte(address.add(i)) & 0xff;
      if (value == 0) break;
      text.append((char) value);
    }
    return text.toString();
  }
}
