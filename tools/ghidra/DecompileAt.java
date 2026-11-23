// Decompile at explicit addresses, forcing a function if Ghidra has not
// created one (retail code is full of gaps that break auto-detection).
//
//   DecompileAt.java <outDir> <addrHex> [addrHex ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;

public class DecompileAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: DecompileAt <outDir> <addr...>");
            return;
        }
        File outDir = new File(args[0]);
        outDir.mkdirs();

        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(true);
        ifc.toggleSyntaxTree(false);
        ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);

        for (int i = 1; i < args.length; i++) {
            long a = Long.parseUnsignedLong(args[i].replace("0x", ""), 16);
            Address addr = toAddr(a);
            Function f = getFunctionAt(addr);
            if (f == null) {
                disassemble(addr);
                f = createFunction(addr, null);
            }
            if (f == null) {
                println("no function at " + args[i]);
                continue;
            }
            File out = new File(outDir, "at_" + String.format("%08X", a) + ".c");
            try (PrintWriter w = new PrintWriter(out)) {
                w.println("// " + f.getName() + " @ " + f.getEntryPoint()
                          + " body=" + f.getBody().getNumAddresses() + " bytes");
                DecompileResults res = ifc.decompileFunction(f, 120, monitor);
                if (res != null && res.decompileCompleted()) {
                    w.print(res.getDecompiledFunction().getC());
                } else {
                    w.println("// decompile failed: "
                              + (res != null ? res.getErrorMessage() : "null"));
                }
            }
            println("decompiled " + args[i]);
        }
        ifc.dispose();
    }
}
