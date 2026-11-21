// Dump functions, strings, and decompilation for a loaded DOL.
// Post-script for analyzeHeadless. Args: <output-dir> [<addr-to-decompile> ...]
//@category ColorsRecomp
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpDol extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("usage: DumpDol.java <output-dir> [addr...]");
            return;
        }
        String outDir = args[0];
        new File(outDir).mkdirs();

        // 1. Functions
        PrintWriter fn = new PrintWriter(new FileWriter(outDir + "/functions.txt"));
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int count = 0;
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            fn.printf("%08X %s %d\n", f.getEntryPoint().getOffset(),
                       f.getName(), (int)f.getBody().getNumAddresses());
            count++;
        }
        fn.close();
        println("functions: " + count);

        // 2. Strings
        PrintWriter st = new PrintWriter(new FileWriter(outDir + "/strings.txt"));
        Listing listing = currentProgram.getListing();
        DataIterator dit = listing.getDefinedData(true);
        int scount = 0;
        while (dit.hasNext() && !monitor.isCancelled()) {
            Data d = dit.next();
            if (d.hasStringValue()) {
                String s = (String)d.getValue();
                if (s.length() >= 3) {
                    st.printf("%08X %s\n", d.getAddress().getOffset(),
                               s.replace('\n', ' ').replace('\r', ' '));
                    scount++;
                }
            }
        }
        st.close();
        println("strings: " + scount);

        // 3. Decompile requested addresses
        if (args.length > 1) {
            DecompInterface dec = new DecompInterface();
            dec.openProgram(currentProgram);
            for (int i = 1; i < args.length; i++) {
                long addr = Long.decode(args[i]);
                Address a = toAddr(addr);
                Function f = getFunctionAt(a);
                String name = String.format("decompile_%08X.c", addr);
                PrintWriter dw = new PrintWriter(new FileWriter(outDir + "/" + name));
                if (f == null) {
                    dw.println("// no function at " + args[i]);
                } else {
                    DecompileResults res = dec.decompileFunction(f, 60, monitor);
                    if (res.decompileCompleted()) {
                        dw.println("// " + f.getName() + " @ " + args[i]);
                        dw.println(res.getDecompiledFunction().getC());
                    } else {
                        dw.println("// decompile failed: " + res.getErrorMessage());
                    }
                }
                dw.close();
                println("decompiled " + args[i] + " -> " + name);
            }
            dec.dispose();
        }
        println("done");
    }
}
