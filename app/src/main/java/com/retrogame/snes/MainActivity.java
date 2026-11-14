package com.retrogame.snes;

import android.Manifest;
import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.MediaStore;
import android.util.Base64;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.Toast;

import androidx.webkit.WebViewAssetLoader;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

/**
 * Tela única do app: a imagem do jogo é desenhada pelo C++ (Vulkan) numa SurfaceView,
 * e por cima fica a página do Retro Game SNES (biblioteca, menus, botões na tela).
 */
public class MainActivity extends Activity {
    private static final int REQ_FILE = 41, REQ_CAMERA = 42;
    private static final String START_URL = "https://appassets.androidplatform.net/assets/index.html";

    private WebView web;
    private SurfaceView surface;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private ValueCallback<Uri[]> fileCallback;
    private PermissionRequest pendingPermission;
    private boolean fullscreen = false;

    // ---------------- ciclo de vida ----------------
    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        NativeCore.init();

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);

        surface = new SurfaceView(this);
        surface.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder h) { NativeCore.setSurface(h.getSurface()); }
            @Override public void surfaceChanged(SurfaceHolder h, int format, int w, int hh) { NativeCore.surfaceChanged(); }
            @Override public void surfaceDestroyed(SurfaceHolder h) { NativeCore.setSurface(null); }
        });
        root.addView(surface, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        web = new WebView(this);
        web.setBackgroundColor(Color.TRANSPARENT);
        root.addView(web, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        setContentView(root);

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(true);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setTextZoom(100);
        s.setLoadWithOverviewMode(false);
        s.setUseWideViewPort(false);
        s.setCacheMode(WebSettings.LOAD_DEFAULT);

        final WebViewAssetLoader loader = new WebViewAssetLoader.Builder()
                .addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this))
                .build();
        web.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                return loader.shouldInterceptRequest(request.getUrl());
            }
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                Uri u = request.getUrl();
                if ("appassets.androidplatform.net".equals(u.getHost())) return false;
                // links externos (WhatsApp, sites) abrem fora do app
                try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (ActivityNotFoundException e) { /* nada */ }
                return true;
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams params) {
                if (fileCallback != null) fileCallback.onReceiveValue(null);
                fileCallback = cb;
                Intent i = new Intent(Intent.ACTION_GET_CONTENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                String type = "*/*";
                String[] accept = params.getAcceptTypes();
                if (accept != null && accept.length == 1 && accept[0] != null && accept[0].contains("/")) type = accept[0];
                i.setType(type);
                if (params.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try {
                    startActivityForResult(Intent.createChooser(i, null), REQ_FILE);
                } catch (ActivityNotFoundException e) {
                    fileCallback = null;
                    return false;
                }
                return true;
            }
            @Override
            public void onPermissionRequest(PermissionRequest request) {
                // câmera para ler o QR das partidas de 2 jogadores
                boolean wantsCamera = false;
                for (String r : request.getResources()) if (PermissionRequest.RESOURCE_VIDEO_CAPTURE.equals(r)) wantsCamera = true;
                if (!wantsCamera) { request.deny(); return; }
                if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
                    request.grant(new String[]{PermissionRequest.RESOURCE_VIDEO_CAPTURE});
                } else {
                    pendingPermission = request;
                    requestPermissions(new String[]{Manifest.permission.CAMERA}, REQ_CAMERA);
                }
            }
        });
        web.addJavascriptInterface(new Bridge(), "NativeSNES");
        if (saved != null) web.restoreState(saved);
        if (web.getUrl() == null) web.loadUrl(START_URL);

        InputManager im = (InputManager) getSystemService(INPUT_SERVICE);
        if (im != null) im.registerInputDeviceListener(new InputManager.InputDeviceListener() {
            @Override public void onInputDeviceAdded(int id) { }
            @Override public void onInputDeviceRemoved(int id) { padRemoved(id); }
            @Override public void onInputDeviceChanged(int id) { }
        }, ui);
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        web.saveState(out);
    }

    @Override
    protected void onPause() {
        super.onPause();
        NativeCore.setForeground(false);
        web.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        web.onResume();
        NativeCore.setForeground(true);
        if (fullscreen) applyFullscreen(true);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && fullscreen) applyFullscreen(true);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        // o botão Voltar do celular segue a navegação da página (fecha menus, sai do jogo...)
        if (web.canGoBack()) web.goBack();
        else moveTaskToBack(true);
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_FILE || fileCallback == null) return;
        Uri[] result = null;
        if (res == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                int n = data.getClipData().getItemCount();
                result = new Uri[n];
                for (int i = 0; i < n; i++) result[i] = data.getClipData().getItemAt(i).getUri();
            } else if (data.getData() != null) {
                result = new Uri[]{data.getData()};
            }
        }
        fileCallback.onReceiveValue(result);
        fileCallback = null;
    }

    @Override
    public void onRequestPermissionsResult(int req, String[] perms, int[] results) {
        super.onRequestPermissionsResult(req, perms, results);
        if (req == REQ_CAMERA && pendingPermission != null) {
            if (results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED)
                pendingPermission.grant(new String[]{PermissionRequest.RESOURCE_VIDEO_CAPTURE});
            else pendingPermission.deny();
            pendingPermission = null;
        }
    }

    // ---------------- tela cheia ----------------
    @SuppressWarnings("deprecation")
    private void applyFullscreen(boolean on) {
        Window w = getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(!on);
            WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                if (on) {
                    c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                    c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                } else {
                    c.show(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                }
            }
        } else {
            View d = w.getDecorView();
            d.setSystemUiVisibility(on
                    ? View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                      | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    : View.SYSTEM_UI_FLAG_VISIBLE);
        }
    }

    // ---------------- controles (USB / Bluetooth) ----------------
    // bits do controle do SNES: R4 L5 X6 A7 →8 ←9 ↓10 ↑11 START12 SELECT13 Y14 B15
    private static final int B_R = 1 << 4, B_L = 1 << 5, B_X = 1 << 6, B_A = 1 << 7, B_RIGHT = 1 << 8, B_LEFT = 1 << 9,
            B_DOWN = 1 << 10, B_UP = 1 << 11, B_START = 1 << 12, B_SELECT = 1 << 13, B_Y = 1 << 14, B_B = 1 << 15;

    private static final class Pad { int buttons, axes; boolean ff, ffAxis, rw, rwAxis; String name; }
    private final SparseArray<Pad> pads = new SparseArray<>();
    private final ArrayList<Integer> padOrder = new ArrayList<>();
    private volatile String padStateStr = "0,0,0,0";
    private volatile String padNamesStr = "[]";

    private static boolean isPadSource(int src) {
        return (src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private Pad padFor(InputDevice dev) {
        int id = dev.getId();
        Pad p = pads.get(id);
        if (p == null) {
            p = new Pad();
            p.name = dev.getName();
            pads.put(id, p);
            padOrder.add(id);
            publishPads(true);
        }
        return p;
    }

    private void padRemoved(int id) {
        if (pads.get(id) == null) return;
        pads.remove(id);
        padOrder.remove(Integer.valueOf(id));
        publishPads(false);
        web.evaluateJavascript("window.dispatchEvent(new Event('gamepaddisconnected'))", null);
    }

    private void publishPads(boolean added) {
        int m1 = 0, m2 = 0;
        boolean ff = false, rw = false;
        StringBuilder names = new StringBuilder("[");
        for (int i = 0; i < padOrder.size(); i++) {
            Pad p = pads.get(padOrder.get(i));
            if (p == null) continue;
            int m = p.buttons | p.axes;
            if (i == 1) m2 |= m; else m1 |= m;
            ff |= p.ff || p.ffAxis;
            rw |= p.rw || p.rwAxis;
            if (names.length() > 1) names.append(',');
            names.append('"').append(p.name == null ? "Controle" : p.name.replace("\\", "").replace("\"", "")).append('"');
        }
        names.append(']');
        padNamesStr = names.toString();
        padStateStr = m1 + "," + m2 + "," + (ff ? 1 : 0) + "," + (rw ? 1 : 0);
        NativeCore.setPad(0, m1);
        NativeCore.setPad(1, m2);
        if (added) web.evaluateJavascript("window.dispatchEvent(new Event('gamepadconnected'))", null);
    }

    private static int keyBit(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: return B_B;      // botão de baixo
            case KeyEvent.KEYCODE_BUTTON_B: return B_A;      // botão da direita
            case KeyEvent.KEYCODE_BUTTON_X: return B_Y;      // botão da esquerda
            case KeyEvent.KEYCODE_BUTTON_Y: return B_X;      // botão de cima
            case KeyEvent.KEYCODE_BUTTON_L1: return B_L;
            case KeyEvent.KEYCODE_BUTTON_R1: return B_R;
            case KeyEvent.KEYCODE_BUTTON_START: return B_START;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return B_SELECT;
            case KeyEvent.KEYCODE_DPAD_UP: return B_UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return B_DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return B_LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return B_RIGHT;
            default: return 0;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        InputDevice dev = e.getDevice();
        int code = e.getKeyCode();
        if (dev != null && isPadSource(e.getSource()) && code != KeyEvent.KEYCODE_BACK) {
            boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
            if (e.getAction() == KeyEvent.ACTION_MULTIPLE) return true;
            Pad p = padFor(dev);
            int bit = keyBit(code);
            if (bit != 0) {
                if (down) p.buttons |= bit; else p.buttons &= ~bit;
            } else if (code == KeyEvent.KEYCODE_BUTTON_R2) {
                p.ff = down;               // R2 = avançar rápido
            } else if (code == KeyEvent.KEYCODE_BUTTON_L2) {
                p.rw = down;               // L2 = voltar no tempo
            } else if (code == KeyEvent.KEYCODE_BUTTON_MODE) {
                if (down) web.evaluateJavascript("window.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape'}))", null);
            } else {
                return super.dispatchKeyEvent(e);
            }
            publishPads(false);
            return true;
        }
        return super.dispatchKeyEvent(e);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent e) {
        InputDevice dev = e.getDevice();
        if (dev != null && (e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK && e.getAction() == MotionEvent.ACTION_MOVE) {
            Pad p = padFor(dev);
            float x = e.getAxisValue(MotionEvent.AXIS_X), y = e.getAxisValue(MotionEvent.AXIS_Y);
            float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X), hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
            float rt = Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS));
            float lt = Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE));
            int m = 0;
            if (x > 0.5f || hx > 0.5f) m |= B_RIGHT;
            if (x < -0.5f || hx < -0.5f) m |= B_LEFT;
            if (y > 0.5f || hy > 0.5f) m |= B_DOWN;
            if (y < -0.5f || hy < -0.5f) m |= B_UP;
            p.axes = m;
            p.ffAxis = rt > 0.5f;
            p.rwAxis = lt > 0.5f;
            publishPads(false);
            return true;
        }
        return super.dispatchGenericMotionEvent(e);
    }

    // ---------------- ponte com a página (window.NativeSNES) ----------------
    private static byte[] dec(String b64) { return b64 == null || b64.isEmpty() ? new byte[0] : Base64.decode(b64, Base64.NO_WRAP); }
    private static String enc(byte[] b) { return b == null ? "" : Base64.encodeToString(b, Base64.NO_WRAP); }

    private final class Bridge {
        private final Map<Integer, OutputStream> saves = new HashMap<>();
        private final Map<Integer, String> saveNames = new HashMap<>();
        private final Map<Integer, Uri> saveUris = new HashMap<>();
        private int nextSave = 1;

        @JavascriptInterface public boolean loadRom(String b64) { return NativeCore.loadRom(dec(b64)); }
        @JavascriptInterface public void reset() { NativeCore.reset(); }
        @JavascriptInterface public String getFrame() { return enc(NativeCore.getFrame()); }
        @JavascriptInterface public String getState() { return enc(NativeCore.getState()); }
        @JavascriptInterface public boolean setState(String b64) { return NativeCore.setState(dec(b64)); }
        @JavascriptInterface public int stateHash() { return NativeCore.stateHash(); }
        @JavascriptInterface public String getSave() { return enc(NativeCore.getSave()); }
        @JavascriptInterface public void setSave(String b64) { NativeCore.setSave(dec(b64)); }
        @JavascriptInterface public int saveType() { return NativeCore.saveType(); }
        @JavascriptInterface public String chipWarning() { return NativeCore.chipWarning(); }
        @JavascriptInterface public int setCheats(String text) { return NativeCore.setCheats(text); }
        @JavascriptInterface public void setKeys(int k1, int k2) { NativeCore.setKeys(k1, k2); }
        @JavascriptInterface public void setRun(boolean run, double speed, boolean audio) { NativeCore.setRun(run, (float) speed, audio); }
        @JavascriptInterface public void setExternal(boolean ext) { NativeCore.setExternal(ext); }
        @JavascriptInterface public void stepFrame() { NativeCore.stepFrame(); }
        @JavascriptInterface public void setRewind(boolean held) { NativeCore.setRewind(held); }
        @JavascriptInterface public void setRewindEnabled(boolean on) { NativeCore.setRewindEnabled(on); }
        @JavascriptInterface public void setView(double x, double y, double w, double h, int rot, int filter) { NativeCore.setView((float) x, (float) y, (float) w, (float) h, rot, filter); }
        @JavascriptInterface public void setColorFix(boolean on) { NativeCore.setColorFix(on); }
        @JavascriptInterface public void setVolume(double v) { NativeCore.setVolume((float) v); }
        @JavascriptInterface public boolean takeSaveDirty() { return NativeCore.takeSaveDirty(); }
        @JavascriptInterface public int fps() { return NativeCore.fps(); }
        @JavascriptInterface public boolean vulkan() { return NativeCore.vulkan(); }
        @JavascriptInterface public String padState() { return padStateStr; }
        @JavascriptInterface public String padNames() { return padNamesStr; }

        @JavascriptInterface public void setOrientation(String o) {
            final int req = "land".equals(o) ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                    : "port".equals(o) ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                    : ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
            ui.post(() -> setRequestedOrientation(req));
        }
        @JavascriptInterface public void setFullscreen(boolean on) { ui.post(() -> { fullscreen = on; applyFullscreen(on); }); }
        @JavascriptInterface public void setKeepScreenOn(boolean on) {
            ui.post(() -> { if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); });
        }
        @JavascriptInterface public void vibrate(int ms) {
            Vibrator v = (Vibrator) getSystemService(VIBRATOR_SERVICE);
            if (v == null || !v.hasVibrator()) return;
            if (ms <= 0) { v.cancel(); return; }
            v.vibrate(VibrationEffect.createOneShot(Math.min(ms, 2000), VibrationEffect.DEFAULT_AMPLITUDE));
        }
        @JavascriptInterface public void shareText(String text) {
            ui.post(() -> {
                Intent i = new Intent(Intent.ACTION_SEND);
                i.setType("text/plain");
                i.putExtra(Intent.EXTRA_TEXT, text);
                try { startActivity(Intent.createChooser(i, null)); } catch (ActivityNotFoundException e) { /* nada */ }
            });
        }

        // salvar arquivos (exportar, capturas, saves) na pasta Downloads
        @JavascriptInterface public synchronized int saveBegin(String name, String mime) {
            String safe = name.replaceAll("[\\\\/:*?\"<>|]+", "_");
            try {
                OutputStream os;
                Uri uri = null;
                if (Build.VERSION.SDK_INT >= 29) {
                    ContentValues cv = new ContentValues();
                    cv.put(MediaStore.MediaColumns.DISPLAY_NAME, safe);
                    cv.put(MediaStore.MediaColumns.MIME_TYPE, mime == null || mime.isEmpty() ? "application/octet-stream" : mime);
                    cv.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/RetroGameSNES");
                    uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv);
                    if (uri == null) return 0;
                    os = getContentResolver().openOutputStream(uri);
                } else {
                    File dir = getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
                    if (dir == null) return 0;
                    dir.mkdirs();
                    os = new FileOutputStream(new File(dir, safe));
                }
                if (os == null) return 0;
                int id = nextSave++;
                saves.put(id, os);
                saveNames.put(id, safe);
                if (uri != null) saveUris.put(id, uri);
                return id;
            } catch (Exception e) {
                return 0;
            }
        }
        @JavascriptInterface public synchronized boolean saveChunk(int id, String b64) {
            OutputStream os = saves.get(id);
            if (os == null) return false;
            try { os.write(dec(b64)); return true; } catch (Exception e) { return false; }
        }
        @JavascriptInterface public synchronized String saveEnd(int id) {
            OutputStream os = saves.remove(id);
            String name = saveNames.remove(id);
            saveUris.remove(id);
            if (os == null) return "";
            try {
                os.close();
                final String where = (Build.VERSION.SDK_INT >= 29 ? "Download/RetroGameSNES/" : "pasta do app/Download/") + name;
                ui.post(() -> Toast.makeText(MainActivity.this, "Salvo em " + where, Toast.LENGTH_SHORT).show());
                return where;
            } catch (Exception e) {
                return "";
            }
        }
    }
}
