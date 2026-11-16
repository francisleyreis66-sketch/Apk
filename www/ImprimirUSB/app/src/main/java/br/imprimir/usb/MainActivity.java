package br.imprimir.usb;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.PendingIntent;
import android.content.ContentValues;
import android.content.Intent;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.database.Cursor;
import android.util.Base64;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/** Mostra a interface "Imprimir" (página em assets/index.html) e liga os botões ao driver USB. */
public class MainActivity extends Activity {
    private static final int PICK_FILES = 1, PICK_FW = 2;
    private static final String PERM = "br.imprimir.usb.PERMISSAO";

    private WebView web;
    private ValueCallback<Uri[]> fileCb;
    private Uri pendingShare;
    private boolean pageReady;
    private volatile boolean busy;

    @SuppressLint("SetJavaScriptEnabled")
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setMediaPlaybackRequiresUserGesture(true);
        web.addJavascriptInterface(new Bridge(), "AndroidUSB");
        web.setWebViewClient(new WebViewClient() {
            @Override public void onPageFinished(WebView v, String url) {
                pageReady = true;
                if (pendingShare != null) { Uri u = pendingShare; pendingShare = null; sendToPage(u); }
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
                if (fileCb != null) fileCb.onReceiveValue(null);
                fileCb = cb;
                Intent i = new Intent(Intent.ACTION_GET_CONTENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                List<String> mimes = new ArrayList<>();
                for (String t : p.getAcceptTypes()) for (String x : t.split(",")) if (x.trim().contains("/")) mimes.add(x.trim());
                if (!mimes.isEmpty()) i.putExtra(Intent.EXTRA_MIME_TYPES, mimes.toArray(new String[0]));
                if (p.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try { startActivityForResult(Intent.createChooser(i, "Escolher arquivo"), PICK_FILES); }
                catch (Exception e) { fileCb = null; return false; }
                return true;
            }
        });
        setContentView(web);
        try {
            web.loadDataWithBaseURL("https://imprimir.app/", readAsset("index.html"), "text/html", "utf-8", null);
        } catch (Exception e) {
            web.loadData("Erro ao abrir a interface: " + e.getMessage(), "text/plain", "utf-8");
        }
        handle(getIntent());
    }

    @Override protected void onNewIntent(Intent i) { super.onNewIntent(i); setIntent(i); handle(i); }
    @Override protected void onResume() { super.onResume(); js("appRefresh()"); }
    @Override public void onBackPressed() { if (web.canGoBack()) web.goBack(); else super.onBackPressed(); }

    private String readAsset(String name) throws Exception {
        try (InputStream in = getAssets().open(name)) { return new String(readAll(in), "UTF-8"); }
    }

    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] b = new byte[65536]; int n;
        while ((n = in.read(b)) > 0) bo.write(b, 0, n);
        return bo.toByteArray();
    }

    // ---------- conversa com a página ----------
    private void js(String code) { runOnUiThread(() -> { if (pageReady) web.evaluateJavascript(code, null); }); }
    private void say(String msg) { js("st(" + JSONObject.quote(msg) + ")"); }

    /** Arquivo vindo de "Compartilhar" / "Abrir com" de outro app. */
    private void handle(Intent i) {
        if (i == null) return;
        Uri u = null;
        if (Intent.ACTION_SEND.equals(i.getAction())) u = i.getParcelableExtra(Intent.EXTRA_STREAM);
        else if (Intent.ACTION_VIEW.equals(i.getAction())) u = i.getData();
        else if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(i.getAction())) js("appRefresh()");
        if (u == null) return;
        if (pageReady) sendToPage(u); else pendingShare = u;
    }

    private void sendToPage(Uri u) {
        try (InputStream in = getContentResolver().openInputStream(u)) {
            if (in == null) return;
            String b64 = Base64.encodeToString(readAll(in), Base64.NO_WRAP);
            String type = getContentResolver().getType(u);
            js("openShared(" + JSONObject.quote(b64) + "," + JSONObject.quote(nameOf(u)) + "," + JSONObject.quote(type == null ? "" : type) + ")");
        } catch (Exception e) {
            say("Não consegui abrir o arquivo recebido: " + e.getMessage());
        }
    }

    private String nameOf(Uri u) {
        try (Cursor c = getContentResolver().query(u, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) { String n = c.getString(0); if (n != null) return n; }
        } catch (Exception ignored) {}
        String p = u.getLastPathSegment();
        return p != null ? p : "arquivo";
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == PICK_FILES) {
            Uri[] out = null;
            if (res == RESULT_OK && data != null) {
                if (data.getClipData() != null) {
                    out = new Uri[data.getClipData().getItemCount()];
                    for (int i = 0; i < out.length; i++) out[i] = data.getClipData().getItemAt(i).getUri();
                } else if (data.getData() != null) out = new Uri[]{data.getData()};
            }
            if (fileCb != null) fileCb.onReceiveValue(out);
            fileCb = null;
        } else if (req == PICK_FW && res == RESULT_OK && data != null && data.getData() != null) {
            try (InputStream in = getContentResolver().openInputStream(data.getData());
                 OutputStream os = new FileOutputStream(Job.firmwareFile(this))) {
                os.write(readAll(in));
                say("Firmware salvo (" + Job.firmwareFile(this).length() / 1024 + " KB). O app envia quando a impressora pedir.");
            } catch (Exception e) { say("Erro ao salvar firmware: " + e.getMessage()); }
        }
    }

    // ---------- impressora ----------
    private String printerText() {
        UsbManager um = (UsbManager) getSystemService(USB_SERVICE);
        UsbDevice d = Job.find(um);
        if (d == null) return "Nenhuma impressora no cabo. Ligue com o adaptador OTG.";
        String fw = Job.firmwareFile(this).exists() ? " · firmware carregado" : "";
        return (um.hasPermission(d) ? "✓ " : "⚠ ") + Job.name(d) + (um.hasPermission(d) ? " pronta" : " (toque em autorizar)") + fw;
    }

    private boolean authorize() {
        UsbManager um = (UsbManager) getSystemService(USB_SERVICE);
        UsbDevice d = Job.find(um);
        if (d == null) { say("Nenhuma impressora encontrada no cabo USB."); js("appRefresh()"); return false; }
        if (um.hasPermission(d)) { js("appRefresh()"); return true; }
        Intent i = new Intent(PERM).setPackage(getPackageName());
        um.requestPermission(d, PendingIntent.getBroadcast(this, 0, i, PendingIntent.FLAG_MUTABLE));
        say("Toque em PERMITIR na janela do Android e depois em Imprimir de novo.");
        return false;
    }

    private void printPdf(byte[] pdfBytes, String paperKey, int copies) {
        if (busy) { say("Já existe uma impressão em andamento."); return; }
        if (!authorize()) return;
        busy = true;
        new Thread(() -> {
            String msg;
            File pdf = new File(getCacheDir(), "pagina.pdf");
            try {
                try (OutputStream os = new FileOutputStream(pdf)) { os.write(pdfBytes); }
                try (Raster.Source src = new Raster.PdfSource(pdf, 0)) {
                    Job.print(this, src, null, Job.paperByKey(paperKey), Math.max(1, copies), this::say);
                }
                msg = "Enviado ✓ Aguarde a impressora terminar.";
            } catch (Throwable e) {
                msg = "Não imprimiu: " + e.getMessage();
            } finally {
                pdf.delete();
                busy = false;
            }
            say(msg);
            js("appRefresh()");
        }).start();
    }

    private void savePdf(byte[] bytes, String name) {
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                ContentValues v = new ContentValues();
                v.put(MediaStore.Downloads.DISPLAY_NAME, name);
                v.put(MediaStore.Downloads.MIME_TYPE, "application/pdf");
                Uri u = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
                if (u == null) throw new Exception("sem acesso a Downloads");
                try (OutputStream os = getContentResolver().openOutputStream(u)) { os.write(bytes); }
                say("PDF salvo em Downloads: " + name);
            } else {
                File dir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
                File f = new File(dir, name);
                try (OutputStream os = new FileOutputStream(f)) { os.write(bytes); }
                say("PDF salvo em " + f.getPath());
            }
        } catch (Exception e) { say("Não consegui salvar o PDF: " + e.getMessage()); }
    }

    /** Funções chamadas pela página (window.AndroidUSB.*). */
    public final class Bridge {
        @JavascriptInterface public String printerName() { return printerText(); }
        @JavascriptInterface public void authorize() { runOnUiThread(MainActivity.this::authorize); }
        @JavascriptInterface public String getExtra() { return Job.prefs(MainActivity.this).getString("extra", ""); }
        @JavascriptInterface public void setExtra(String s) { Job.prefs(MainActivity.this).edit().putString("extra", s == null ? "" : s.trim()).apply(); }
        @JavascriptInterface public void pickFirmware() {
            runOnUiThread(() -> {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                startActivityForResult(i, PICK_FW);
            });
        }
        @JavascriptInterface public void openPrintSettings() {
            runOnUiThread(() -> {
                try { startActivity(new Intent(Settings.ACTION_PRINT_SETTINGS)); }
                catch (Exception e) { startActivity(new Intent(Settings.ACTION_SETTINGS)); }
            });
        }
        @JavascriptInterface public void printPdf(String b64, String paper, int copies) {
            final byte[] bytes = Base64.decode(b64, Base64.DEFAULT);
            runOnUiThread(() -> MainActivity.this.printPdf(bytes, paper, copies));
        }
        @JavascriptInterface public void savePdf(String b64, String name) {
            final byte[] bytes = Base64.decode(b64, Base64.DEFAULT);
            new Thread(() -> MainActivity.this.savePdf(bytes, name == null ? "imprimir.pdf" : name)).start();
        }
    }
}
