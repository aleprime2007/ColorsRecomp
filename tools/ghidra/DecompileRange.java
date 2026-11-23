// Decompile every function in an address range and write one .c per function.
//
// Usage (analyzeHeadless -postScript):
//   DecompileRange.java <loHex> <hiHex> <outDir>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

import java.io.File;
import java.io.PrintWriter;

public class DecompileRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) {
            println("usage: DecompileRange <loHex> <hiHex> <outDir>");
            return;
        }
        long lo = Long.parseUnsignedLong(args[0].replace("0x", ""), 16);
        long hi = Long.parseUnsignedLong(args[1].replace("0x", ""), 16);
        File outDir = new File(args[2]);
        if (!outDir.isDirectory() && !outDir.mkdirs()) {
            println("cannot create " + outDir);
            return;
        }

        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.toggleSyntaxTree(false);
        ifc.setSimplificationStyle("decompile");
        if (!ifc.openProgram(currentProgram)) {
            println("decompiler open failed");
            return;
        }

        FunctionManager fm = currentProgram.getFunctionManager();
        AddressSet set = new AddressSet(toAddr(lo), toAddr(hi));
        int n = 0;
        for (Function f : fm.getFunctions(set, true)) {
            String name = String.format("%08X", f.getEntryPoint().getUnsignedOffset());
            File out = new File(outDir, "fn_" + name + ".c");
            try (PrintWriter w = new PrintWriter(out)) {
                w.println("// " + f.getName() + " @ " + f.getEntryPoint()
                        + (f.isThunk() ? " (thunk)" : ""));
                w.println("// callers: " + f.getCallingFunctions(monitor) + ")");
                DecompileResults res = ifc.decompileFunction(f, 120, monitor);
                if (res != null && res.decompileCompleted()) {
                    w.print(res.getDecompiledFunction().getC());
                } else {
                    w.println("// decompile failed: "
                            + (res != null ? res.getErrorMessage() : "null"));
                }
            }
            n++;
        }
        println("decompiled " + n + " function(s) in range");
        ifc.dispose();
    }
}
