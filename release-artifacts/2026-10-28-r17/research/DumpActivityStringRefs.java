import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.LinkedHashMap;
import java.util.Map;

/** Reports existing references to selected activity-related ELF strings. */
public class DumpActivityStringRefs extends GhidraScript {
  private static final long[] TARGETS = { 0x012FCA24L, 0x012DE1F0L };

  @Override
  protected void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length != 1) throw new IllegalArgumentException("Expected one output path");
    Map<String, Function> functions = new LinkedHashMap<>();
    try (BufferedWriter out = Files.newBufferedWriter(Paths.get(args[0]), StandardCharsets.UTF_8)) {
      out.write("Program: " + currentProgram.getName() + "\n");
      out.write("Language: " + currentProgram.getLanguageID() + "\n");
      for (long raw : TARGETS) {
        Address target = toAddr(raw);
        out.write("\nTARGET " + target + "\n");
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(target);
        int count = 0;
        while (references.hasNext()) {
          Reference reference = references.next();
          Address from = reference.getFromAddress();
          Instruction instruction = currentProgram.getListing().getInstructionAt(from);
          Function function = currentProgram.getFunctionManager().getFunctionContaining(from);
          out.write("  REF " + from + " type=" + reference.getReferenceType() +
              " instruction=" + (instruction == null ? "<none>" : instruction.toString()) +
              " function=" + (function == null ? "<none>" : function.getName() + " " + function.getEntryPoint()) + "\n");
          if (function != null) functions.putIfAbsent(function.getEntryPoint().toString(), function);
          count++;
        }
        if (count == 0) out.write("  <no references in current analysis database>\n");
      }

      DecompInterface decompiler = new DecompInterface();
      decompiler.openProgram(currentProgram);
      try {
        for (Function function : functions.values()) {
          out.write("\nFUNCTION " + function.getName() + " " + function.getEntryPoint() + "\n");
          DecompileResults result = decompiler.decompileFunction(function, 90, monitor);
          if (result != null && result.decompileCompleted() && result.getDecompiledFunction() != null) {
            out.write(result.getDecompiledFunction().getC());
            out.write("\n");
          } else {
            out.write("DECOMPILE FAILED: " + (result == null ? "null result" : result.getErrorMessage()) + "\n");
          }
        }
      } finally {
        decompiler.dispose();
      }
    }
  }
}
