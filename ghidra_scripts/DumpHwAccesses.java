// Dump every instruction that reads or writes BBC Micro memory-mapped I/O ($FC00-$FEFF) or
// an OS vector cell ($0200-$0235).  Output is a markdown table for docs/bbc-hardware.md.
//
// ⭐ THE OUTPUT IS THE ABSTRACTION BOUNDARY.  It says exactly what platform.h must expose, and
// it replaces the [ASSUMED] rows in docs/bbc-hardware.md with measured ones.
//
// ⚠ Run this against disasm/revs_runtime.bin, NOT revs_mem.bin — REVS2 unpacks itself before
// running, so the loaded image is not the layout the engine executes (docs/static-map.md).
//
// This deliberately overlaps tools/sweep_entrypoints.py, which reports the same thing from an
// independent recursive-descent walk.  Two tools disagreeing is information; one tool agreeing
// with itself is not.
//
// Arg0 = output path
//@category BBC
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class DumpHwAccesses extends GhidraScript {

    // Named registers.  SHEILA ($FE00-$FEFF) is the whole of the BBC's own I/O; FRED ($FC00)
    // and JIM ($FD00) are the 1 MHz expansion bus and are named only as ranges, since a hit
    // there would itself be the finding.
    static final Map<Integer, String> NAMES = new LinkedHashMap<>();
    static {
        // 6845 CRTC — $FE00-$FE07, but only two addresses matter: an address register and a
        // data register, so every CRTC register is written as a pair through these.
        NAMES.put(0xFE00, "CRTC_ADDR");   NAMES.put(0xFE01, "CRTC_DATA");
        // 6850 ACIA + serial ULA
        NAMES.put(0xFE08, "ACIA_STATUS"); NAMES.put(0xFE09, "ACIA_DATA");
        NAMES.put(0xFE10, "SERIAL_ULA");
        // Video ULA
        NAMES.put(0xFE20, "ULA_CONTROL"); NAMES.put(0xFE21, "ULA_PALETTE");
        // ROM select / paging
        NAMES.put(0xFE30, "ROMSEL");      NAMES.put(0xFE34, "ACCCON");
        // System VIA $FE40-$FE4F — keyboard, SN76489 sound, ADC start, vsync + timer IRQs
        NAMES.put(0xFE40, "SYSVIA_ORB");  NAMES.put(0xFE41, "SYSVIA_ORA_H");
        NAMES.put(0xFE42, "SYSVIA_DDRB"); NAMES.put(0xFE43, "SYSVIA_DDRA");
        NAMES.put(0xFE44, "SYSVIA_T1CL"); NAMES.put(0xFE45, "SYSVIA_T1CH");
        NAMES.put(0xFE46, "SYSVIA_T1LL"); NAMES.put(0xFE47, "SYSVIA_T1LH");
        NAMES.put(0xFE48, "SYSVIA_T2CL"); NAMES.put(0xFE49, "SYSVIA_T2CH");
        NAMES.put(0xFE4A, "SYSVIA_SR");   NAMES.put(0xFE4B, "SYSVIA_ACR");
        NAMES.put(0xFE4C, "SYSVIA_PCR");  NAMES.put(0xFE4D, "SYSVIA_IFR");
        NAMES.put(0xFE4E, "SYSVIA_IER");  NAMES.put(0xFE4F, "SYSVIA_ORA");
        // User VIA $FE60-$FE6F — printer / user port, and a free-running T1 used as a clock
        NAMES.put(0xFE60, "USRVIA_ORB");  NAMES.put(0xFE61, "USRVIA_ORA_H");
        NAMES.put(0xFE62, "USRVIA_DDRB"); NAMES.put(0xFE63, "USRVIA_DDRA");
        NAMES.put(0xFE64, "USRVIA_T1CL"); NAMES.put(0xFE65, "USRVIA_T1CH");
        NAMES.put(0xFE66, "USRVIA_T1LL"); NAMES.put(0xFE67, "USRVIA_T1LH");
        NAMES.put(0xFE68, "USRVIA_T2CL"); NAMES.put(0xFE69, "USRVIA_T2CH");
        NAMES.put(0xFE6A, "USRVIA_SR");   NAMES.put(0xFE6B, "USRVIA_ACR");
        NAMES.put(0xFE6C, "USRVIA_PCR");  NAMES.put(0xFE6D, "USRVIA_IFR");
        NAMES.put(0xFE6E, "USRVIA_IER");  NAMES.put(0xFE6F, "USRVIA_ORA");
        // Floppy controllers
        NAMES.put(0xFE80, "FDC_8271_CMD"); NAMES.put(0xFE84, "FDC_8271_DATA");
        // uPD7002 ADC — the analogue joystick.  ⭐ Revs does NOT address these directly; it
        // reads the ADC through OSBYTE 128 / OSBYTE 190.  A hit here would be news.
        NAMES.put(0xFEC0, "ADC_STATUS");  NAMES.put(0xFEC1, "ADC_HIGH");
        NAMES.put(0xFEC2, "ADC_LOW");     NAMES.put(0xFEC3, "ADC_CTRL");
        // Tube
        NAMES.put(0xFEE0, "TUBE");
        // OS vectors $0200-$0235
        String[] vec = {"BRKV", "IRQ1V", "IRQ2V", "CLIV", "BYTEV", "WORDV", "WRCHV", "RDCHV",
                        "FILEV", "ARGSV", "BGETV", "BPUTV", "GBPBV", "FINDV", "FSCV", "EVNTV",
                        "UPTV", "NETV", "VDUV", "KEYV", "INSV", "REMV", "CNPV",
                        "IND1V", "IND2V", "IND3V"};
        for (int i = 0; i < vec.length; i++) {
            NAMES.put(0x0202 + i * 2, vec[i]);
            NAMES.put(0x0203 + i * 2, vec[i] + "+1");
        }
        NAMES.put(0x0200, "USERV"); NAMES.put(0x0201, "USERV+1");
    }

    static boolean isHwOrVector(long a) {
        return (a >= 0xFC00 && a <= 0xFEFF) || (a >= 0x0200 && a <= 0x0235);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String out = args.length > 0 ? args[0] : "hw-access.txt";

        Listing listing = currentProgram.getListing();
        Map<Long, List<String>> reads = new TreeMap<>();
        Map<Long, List<String>> writes = new TreeMap<>();
        // Indexed accesses reach base..base+255.  Reporting that whole span as "accessed"
        // makes the table useless (STA $013B,X would read as a write to every OS vector), so
        // an exact base is a finding and an overlapping span is listed separately.
        List<String> spanSuspects = new ArrayList<>();

        InstructionIterator it = listing.getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            long iAddr = ins.getAddress().getOffset();
            Function f = getFunctionContaining(ins.getAddress());
            String fn = (f != null) ? String.format("%s(%04X)", f.getName(), iAddr)
                                    : String.format("?(%04X)", iAddr);
            boolean indexed = ins.toString().matches(".*,\\s*[XY]\\s*$");

            for (int op = 0; op < ins.getNumOperands(); op++) {
                for (Object o : ins.getOpObjects(op)) {
                    if (!(o instanceof Address)) continue;
                    long a = ((Address) o).getOffset();
                    if (isHwOrVector(a)) {
                        if (isWrite(ins)) writes.computeIfAbsent(a, k -> new ArrayList<>()).add(fn);
                        else              reads.computeIfAbsent(a, k -> new ArrayList<>()).add(fn);
                    } else if (indexed && isWrite(ins)
                               && a < 0x0236 && a + 255 >= 0x0202) {
                        spanSuspects.add(String.format("%s  %s", fn, ins.toString()));
                    }
                }
            }
        }

        Set<Long> all = new TreeSet<>();
        all.addAll(reads.keySet());
        all.addAll(writes.keySet());

        PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(out)));
        w.println("# BBC hardware & OS-vector accesses");
        w.println();
        w.println("Generated by DumpHwAccesses.java from disasm/revs_runtime.bin via Ghidra");
        w.println("headless.  Sorted by address.  `R`=read, `W`=write.");
        w.println();
        w.println("| Addr | Name | RW | Device | Access sites |");
        w.println("|---|---|---|---|---|");
        for (long a : all) {
            String name = NAMES.getOrDefault((int) a, String.format("$%04X", a));
            List<String> rs = reads.getOrDefault(a, Collections.emptyList());
            List<String> ws = writes.getOrDefault(a, Collections.emptyList());
            String rw = (!rs.isEmpty() && !ws.isEmpty()) ? "R+W" : (!rs.isEmpty() ? "R" : "W");
            List<String> sites = new ArrayList<>();
            for (String s : ws) sites.add("W:" + s);
            for (String s : rs) sites.add("R:" + s);
            String joined = String.join(", ", sites.subList(0, Math.min(6, sites.size())));
            if (sites.size() > 6) joined += " … +" + (sites.size() - 6) + " more";
            w.printf("| `$%04X` | %-16s | %3s | %-14s | %s |%n", a, name, rw, device(a), joined);
        }
        w.println();
        w.println("## Devices NOT touched");
        w.println();
        for (String s : new String[]{"FRED $FC00-$FCFF", "JIM $FD00-$FDFF",
                                     "uPD7002 ADC $FEC0-$FEDF", "Tube $FEE0-$FEFF"}) {
            long lo = Long.parseLong(s.substring(s.indexOf('$') + 1, s.indexOf('-')), 16);
            long hi = Long.parseLong(s.substring(s.lastIndexOf('$') + 1), 16);
            boolean hit = false;
            for (long a : all) if (a >= lo && a <= hi) hit = true;
            w.printf("- %s — %s%n", s, hit ? "**TOUCHED — this table is out of date**"
                                           : "no access found");
        }
        if (!spanSuspects.isEmpty()) {
            w.println();
            w.println("## Indexed writes whose 256-byte span could reach the vector page");
            w.println();
            w.println("Not evidence of a vector write; listed so a computed one cannot hide.");
            w.println("Bound the index range by hand before dismissing any of these.");
            w.println();
            for (String s : spanSuspects) w.println("- `" + s + "`");
        }
        w.close();
        println("wrote " + out + " — " + all.size() + " distinct HW/vector addresses accessed, "
                + spanSuspects.size() + " indexed-span suspects");
    }

    boolean isWrite(Instruction ins) {
        String mn = ins.getMnemonicString().toUpperCase();
        if (mn.equals("STA") || mn.equals("STX") || mn.equals("STY")) return true;
        // RMW touches the bus twice; report as a write, which is the stricter reading.
        return mn.equals("INC") || mn.equals("DEC") || mn.equals("ASL")
            || mn.equals("LSR") || mn.equals("ROL") || mn.equals("ROR");
    }

    String device(long a) {
        if (a >= 0x0200 && a <= 0x0235) return "OS vector";
        if (a >= 0xFC00 && a < 0xFD00) return "FRED (1 MHz)";
        if (a >= 0xFD00 && a < 0xFE00) return "JIM (1 MHz)";
        if (a >= 0xFE00 && a < 0xFE08) return "6845 CRTC";
        if (a >= 0xFE08 && a < 0xFE20) return "ACIA/serial";
        if (a >= 0xFE20 && a < 0xFE30) return "Video ULA";
        if (a >= 0xFE30 && a < 0xFE40) return "ROM select";
        if (a >= 0xFE40 && a < 0xFE60) return "System VIA";
        if (a >= 0xFE60 && a < 0xFE80) return "User VIA";
        if (a >= 0xFE80 && a < 0xFEA0) return "Floppy FDC";
        if (a >= 0xFEC0 && a < 0xFEE0) return "uPD7002 ADC";
        if (a >= 0xFEE0) return "Tube";
        return "SHEILA (other)";
    }
}
