import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Collects decompiler evidence around activity-load error/status strings in a
 * hash-identified Destiny ELF. This is an offline read-only analysis helper.
 */
public class DumpActivityLoadCallers extends GhidraScript {
  private static final int MAX_STRINGS = 120;
  private static final int MAX_FUNCTIONS = 24;

  @Override
  protected void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length != 1) throw new IllegalArgumentException("Expected one output path");

    Map<String, Function> callers = new LinkedHashMap<>();
    int strings = 0;
    try (BufferedWriter out = Files.newBufferedWriter(Paths.get(args[0]), StandardCharsets.UTF_8)) {
      out.write("Program: " + currentProgram.getName() + "\n");
      out.write("Language: " + currentProgram.getLanguageID() + "\n\n");
      DataIterator data = currentProgram.getListing().getDefinedData(true);
      while (data.hasNext() && strings < MAX_STRINGS && callers.size() < MAX_FUNCTIONS) {
        Data item = data.next();
        Object value = item.getValue();
        if (!(value instanceof String)) continue;
        String text = ((String) value).replace('\n', ' ').replace('\r', ' ');
        String lower = text.toLowerCase(Locale.ROOT);
        if (!lower.contains("activity") ||
            !(lower.contains("load") || lower.contains("ready") || lower.contains("world") ||
              lower.contains("start") || lower.contains("transition") || lower.contains("resource"))) {
          continue;
        }
        strings++;
        out.write("STRING " + item.getAddress() + " " + text + "\n");
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(item.getAddress());
        while (references.hasNext()) {
          Reference reference = references.next();
          Function function = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
          if (function != null) {
            callers.putIfAbsent(function.getEntryPoint().toString(), function);
            out.write("  XREF " + reference.getFromAddress() + " -> " + function.getName() +
                " " + function.getEntryPoint() + "\n");
          }
        }
      }

      out.write("\n=== Decompiled callers (" + callers.size() + ") ===\n");
      DecompInterface decompiler = new DecompInterface();
      decompiler.openProgram(currentProgram);
      try {
        int count = 0;
        for (Function function : callers.values()) {
          if (count++ >= MAX_FUNCTIONS) break;
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
