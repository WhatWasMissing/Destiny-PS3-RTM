import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;

public class DumpSpawnUiFunctions extends GhidraScript {
  private static final long[] TARGETS = {
      0x14023cd30L, // placement preference save
      0x140246fb0L, // placement preference load
      0x14022bde0L, // manual/catalog spawn UI
      0x140230d90L, // placement controls
      0x1402434d0L, // spawn dispatch
      0x140426130L, // placement state -> request descriptor
      0x140435830L, // command packet builder wrapper
      0x140436020L, // native spawn packet writer
      0x14043c180L  // command submit path
  };

  @Override
  protected void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length != 1) throw new IllegalArgumentException("Expected one output path");

    DecompInterface decompiler = new DecompInterface();
    decompiler.openProgram(currentProgram);
    try (BufferedWriter out = Files.newBufferedWriter(Paths.get(args[0]), StandardCharsets.UTF_8)) {
      out.write("Program: " + currentProgram.getName() + "\n");
      for (long rawAddress : TARGETS) {
        Address address = toAddr(rawAddress);
        Function function = currentProgram.getFunctionManager().getFunctionAt(address);
        if (function == null) function = currentProgram.getFunctionManager().getFunctionContaining(address);
        out.write("\nTARGET " + address + " FUNCTION " + (function == null ? "<not found>" : function.getName() + " " + function.getEntryPoint()) + "\n");
        if (function == null) continue;
        DecompileResults result = decompiler.decompileFunction(function, 120, monitor);
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
