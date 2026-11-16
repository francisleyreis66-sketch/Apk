package br.imprimir.usb;

import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.print.PageRange;
import android.print.PrintAttributes;
import android.print.PrinterCapabilitiesInfo;
import android.print.PrinterId;
import android.print.PrinterInfo;
import android.printservice.PrintJob;
import android.printservice.PrintService;
import android.printservice.PrinterDiscoverySession;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/** Faz a impressora USB aparecer no menu "Imprimir" de qualquer app do Android. */
public class UsbPrintService extends PrintService {
    private final Handler main = new Handler(Looper.getMainLooper());

    @Override protected PrinterDiscoverySession onCreatePrinterDiscoverySession() {
        return new PrinterDiscoverySession() {
            private void refresh() {
                UsbManager um = (UsbManager) getSystemService(USB_SERVICE);
                UsbDevice d = Job.find(um);
                PrinterId id = generatePrinterId("usb");
                if (d == null) {
                    List<PrinterId> old = new ArrayList<>();
                    for (PrinterInfo p : getPrinters()) old.add(p.getId());
                    if (!old.isEmpty()) removePrinters(old);
                    return;
                }
                PrinterCapabilitiesInfo.Builder cb = new PrinterCapabilitiesInfo.Builder(id)
                        .addMediaSize(PrintAttributes.MediaSize.ISO_A4, true)
                        .addMediaSize(PrintAttributes.MediaSize.NA_LETTER, false)
                        .addMediaSize(PrintAttributes.MediaSize.NA_LEGAL, false)
                        .addMediaSize(PrintAttributes.MediaSize.ISO_A5, false)
                        .addResolution(new PrintAttributes.Resolution("600", "600 dpi", Job.DPI, Job.DPI), true)
                        .setColorModes(PrintAttributes.COLOR_MODE_MONOCHROME, PrintAttributes.COLOR_MODE_MONOCHROME)
                        .setMinMargins(new PrintAttributes.Margins(200, 200, 200, 200));
                int st = um.hasPermission(d) ? PrinterInfo.STATUS_IDLE : PrinterInfo.STATUS_UNAVAILABLE;
                String desc = um.hasPermission(d) ? "Cabo USB" : "Abra o app Imprimir USB para autorizar";
                List<PrinterInfo> l = new ArrayList<>();
                l.add(new PrinterInfo.Builder(id, Job.name(d), st).setDescription(desc).setCapabilities(cb.build()).build());
                addPrinters(l);
            }
            @Override public void onStartPrinterDiscovery(List<PrinterId> priority) { refresh(); }
            @Override public void onStopPrinterDiscovery() {}
            @Override public void onValidatePrinters(List<PrinterId> ids) { refresh(); }
            @Override public void onStartPrinterStateTracking(PrinterId id) { refresh(); }
            @Override public void onStopPrinterStateTracking(PrinterId id) {}
            @Override public void onDestroy() {}
        };
    }

    @Override protected void onRequestCancelPrintJob(PrintJob job) { job.cancel(); }

    @Override protected void onPrintJobQueued(final PrintJob job) {
        if (!job.start() && !job.isStarted()) return;
        final ParcelFileDescriptor fd = job.getDocument().getData();
        final int copies = Math.max(1, job.getInfo().getCopies());
        PrintAttributes.MediaSize ms = job.getInfo().getAttributes().getMediaSize();
        final Job.Paper paper = ms != null ? Job.paperByMils(ms.getWidthMils(), ms.getHeightMils()) : Job.PAPERS[0];
        final PageRange[] ranges = job.getInfo().getPages();
        if (fd == null) { job.fail("documento vazio"); return; }

        new Thread(() -> {
            String err = null;
            File pdf = new File(getCacheDir(), "servico.pdf");
            try {
                try (InputStream in = new FileInputStream(fd.getFileDescriptor()); OutputStream os = new FileOutputStream(pdf)) {
                    byte[] b = new byte[65536]; int n;
                    while ((n = in.read(b)) > 0) os.write(b, 0, n);
                } finally {
                    try { fd.close(); } catch (Exception ignored) {}
                }
                try (Raster.Source src = new Raster.PdfSource(pdf, 0)) {
                    List<Integer> pages = null;
                    if (ranges != null && ranges.length > 0 && !PageRange.ALL_PAGES.equals(ranges[0])) {
                        pages = new ArrayList<>();
                        for (PageRange r : ranges)
                            for (int i = r.getStart(); i <= r.getEnd() && i < src.count(); i++) pages.add(i);
                    }
                    final List<Integer> pg = pages;
                    Job.print(this, src, pg, paper, copies, s -> {});
                }
            } catch (Throwable e) {
                err = e.getMessage() != null ? e.getMessage() : e.toString();
            } finally {
                pdf.delete();
            }
            final String fe = err;
            main.post(() -> { if (fe == null) job.complete(); else job.fail(fe); });
        }).start();
    }
}
