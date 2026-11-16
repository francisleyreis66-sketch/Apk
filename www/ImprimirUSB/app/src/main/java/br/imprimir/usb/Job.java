package br.imprimir.usb;

import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/** Trabalho de impressão: página → PBM → foo2xqx (driver) → cabo USB. */
public final class Job {
    private Job() {}

    public interface Status { void say(String s); }

    /** Papel: nome, código do driver (-p), largura e altura em mm. */
    public static final class Paper {
        public final String name; public final int code; public final double wmm, hmm;
        Paper(String n, int c, double w, double h) { name = n; code = c; wmm = w; hmm = h; }
        public int wpx() { return (int) Math.round(wmm / 25.4 * DPI); }
        public int hpx() { return (int) Math.round(hmm / 25.4 * DPI); }
        @Override public String toString() { return name; }
    }

    public static final int DPI = 600;
    public static final Paper[] PAPERS = {
            new Paper("A4", 9, 210, 297),
            new Paper("Carta", 1, 215.9, 279.4),
            new Paper("Ofício / Legal", 5, 215.9, 355.6),
            new Paper("A5", 11, 148, 210),
            new Paper("Executivo", 7, 184.15, 266.7),
    };

    /** Papel escolhido na interface (valores do campo "Papel"). */
    public static Paper paperByKey(String k) {
        if (k == null) return PAPERS[0];
        switch (k) {
            case "letter": return PAPERS[1];
            case "oficio": case "legal": return PAPERS[2];
            case "a5": return PAPERS[3];
            case "exec": return PAPERS[4];
            default: return PAPERS[0]; // A4, Universal e outros
        }
    }

    public static Paper paperByMils(int wMils, int hMils) {
        int w = Math.min(wMils, hMils), h = Math.max(wMils, hMils);
        Paper best = PAPERS[0]; double d = 1e9;
        for (Paper p : PAPERS) {
            double e = Math.abs(p.wmm / 25.4 * 1000 - w) + Math.abs(p.hmm / 25.4 * 1000 - h);
            if (e < d) { d = e; best = p; }
        }
        return best;
    }

    public static SharedPreferences prefs(Context c) { return c.getSharedPreferences("cfg", Context.MODE_PRIVATE); }
    public static File firmwareFile(Context c) { return new File(c.getFilesDir(), "firmware.bin"); }

    // ---------- USB ----------
    public static UsbInterface printerIface(UsbDevice d) {
        for (int i = 0; i < d.getInterfaceCount(); i++) {
            UsbInterface itf = d.getInterface(i);
            if (itf.getInterfaceClass() == UsbConstants.USB_CLASS_PRINTER && outEp(itf) != null) return itf;
        }
        return null;
    }

    static UsbEndpoint outEp(UsbInterface itf) {
        for (int i = 0; i < itf.getEndpointCount(); i++) {
            UsbEndpoint e = itf.getEndpoint(i);
            if (e.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK && e.getDirection() == UsbConstants.USB_DIR_OUT) return e;
        }
        return null;
    }

    public static UsbDevice find(UsbManager um) {
        for (UsbDevice d : um.getDeviceList().values()) if (printerIface(d) != null) return d;
        return null;
    }

    public static String name(UsbDevice d) {
        String m = d.getManufacturerName(), p = d.getProductName();
        String s = ((m != null ? m + " " : "") + (p != null ? p : "")).trim();
        return s.isEmpty() ? "Impressora USB" : s;
    }

    /** Lê o "Device ID" da impressora (classe de impressora, pedido GET_DEVICE_ID). */
    static String deviceId(UsbDeviceConnection c, UsbInterface itf) {
        byte[] b = new byte[1024];
        int n = c.controlTransfer(0xA1, 0, 0, (itf.getId() << 8) | itf.getAlternateSetting(), b, b.length, 5000);
        if (n < 2) return "";
        int len = Math.min(((b[0] & 255) << 8) | (b[1] & 255), n);
        try { return new String(b, 2, Math.max(0, len - 2), "ISO-8859-1"); } catch (Exception e) { return ""; }
    }

    static void sendFile(UsbDeviceConnection c, UsbEndpoint ep, File f, Status st, String what) throws IOException {
        long total = Math.max(1, f.length()), done = 0;
        byte[] buf = new byte[16384];
        try (InputStream in = new FileInputStream(f)) {
            int n;
            while ((n = in.read(buf)) > 0) {
                int off = 0;
                while (off < n) {
                    byte[] part = buf;
                    if (off > 0) { part = new byte[n - off]; System.arraycopy(buf, off, part, 0, n - off); }
                    int w = c.bulkTransfer(ep, part, n - off, 60000);
                    if (w < 0) throw new IOException("a impressora não aceitou os dados (cabo solto ou impressora ocupada)");
                    off += w;
                }
                done += n;
                st.say(what + " " + (done * 100 / total) + "%");
            }
        }
    }

    // ---------- Driver (foo2xqx) ----------
    static File convert(Context c, File pbm, Paper p, int copies, Status st) throws Exception {
        File exe = new File(c.getApplicationInfo().nativeLibraryDir, "libfoo2xqx.so");
        if (!exe.exists()) throw new IOException("driver não encontrado no app (" + exe + ")");
        List<String> cmd = new ArrayList<>();
        cmd.add(exe.getPath());
        cmd.add("-r" + DPI + "x" + DPI);
        cmd.add("-g" + p.wpx() + "x" + p.hpx());
        cmd.add("-p" + p.code);
        cmd.add("-n" + Math.max(1, copies));
        String extra = prefs(c).getString("extra", "").trim();
        if (!extra.isEmpty()) for (String a : extra.split("\\s+")) cmd.add(a);
        File out = new File(c.getCacheDir(), "job.prn"), err = new File(c.getCacheDir(), "driver-erro.txt");
        st.say("Convertendo para a linguagem da impressora…");
        Process pr = new ProcessBuilder(cmd).redirectInput(pbm).redirectOutput(out).redirectError(err).start();
        int rc = pr.waitFor();
        if (rc != 0 || out.length() == 0) throw new IOException("o driver falhou (código " + rc + "): " + readSmall(err));
        return out;
    }

    static String readSmall(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[600]; int n = in.read(b);
            return n > 0 ? new String(b, 0, n, "UTF-8").trim() : "";
        } catch (Exception e) { return ""; }
    }

    /**
     * Imprime as páginas escolhidas da fonte. pages == null = todas.
     * Roda fora da thread principal.
     */
    public static void print(Context c, Raster.Source src, List<Integer> pages, Paper paper, int copies, Status st) throws Exception {
        UsbManager um = (UsbManager) c.getSystemService(Context.USB_SERVICE);
        UsbDevice d = find(um);
        if (d == null) throw new IOException("nenhuma impressora no cabo USB. Verifique o cabo e o adaptador OTG.");
        if (!um.hasPermission(d)) throw new SecurityException("sem permissão para usar a impressora. Abra o app Imprimir USB e toque em Imprimir para autorizar.");

        // 1) páginas → PBM
        File pbm = new File(c.getCacheDir(), "paginas.pbm");
        int W = paper.wpx(), H = paper.hpx(), n = src.count();
        List<Integer> list = new ArrayList<>();
        if (pages == null) for (int i = 0; i < n; i++) list.add(i);
        else for (int i : pages) if (i >= 0 && i < n) list.add(i);
        if (list.isEmpty()) throw new IOException("nenhuma página para imprimir");
        try (OutputStream os = new BufferedOutputStream(new FileOutputStream(pbm), 1 << 16)) {
            int k = 0;
            for (int i : list) {
                st.say("Preparando página " + (++k) + " de " + list.size() + "…");
                try (Raster.Page p = src.open(i)) { Raster.writePbm(p, W, H, os); }
            }
        }

        // 2) PBM → linguagem da impressora
        File prn = convert(c, pbm, paper, copies, st);
        pbm.delete();

        // 3) envia pelo cabo
        UsbInterface itf = printerIface(d);
        UsbEndpoint ep = outEp(itf);
        UsbDeviceConnection con = um.openDevice(d);
        if (con == null) throw new IOException("não consegui abrir a impressora USB");
        try {
            if (!con.claimInterface(itf, true)) throw new IOException("a impressora está sendo usada por outro app (feche HP Smart, Mopria, PrinterShare)");
            String id = deviceId(con, itf);
            File fw = firmwareFile(c);
            boolean needsFw = id.contains("MDL:") && !id.contains("FWVER");
            if (needsFw) {
                if (fw.exists() && fw.length() > 0) {
                    sendFile(con, ep, fw, st, "Enviando firmware para a impressora…");
                    st.say("Aguardando a impressora iniciar o firmware…");
                    Thread.sleep(8000);
                } else {
                    st.say("Aviso: a impressora parece estar sem firmware. Se não imprimir, carregue o arquivo de firmware no app.");
                    Thread.sleep(1500);
                }
            }
            sendFile(con, ep, prn, st, "Enviando para " + name(d) + "…");
        } finally {
            try { con.releaseInterface(itf); } catch (Exception ignored) {}
            con.close();
            prn.delete();
        }
    }
}
