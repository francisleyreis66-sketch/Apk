package br.imprimir.usb;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.pdf.PdfRenderer;
import android.os.ParcelFileDescriptor;

import java.io.Closeable;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.util.Arrays;

/** Converte PDF ou foto em páginas preto e branco (PBM) a 600 dpi, em faixas para caber na memória. */
public final class Raster {
    private Raster() {}

    public interface Page extends Closeable {
        /** Desenha a faixa que começa na linha y0 da folha (largura W, altura H em pixels). */
        void draw(Bitmap strip, int y0, int W, int H);
        /** Fotos usam pontilhado; documentos usam corte nítido. */
        boolean photo();
        @Override void close();
    }

    public interface Source extends Closeable {
        int count();
        Page open(int i) throws IOException;
        @Override void close();
    }

    /** Matriz que encaixa um conteúdo cw×ch na folha W×H com margem m, girando se precisar. */
    static Matrix fit(float cw, float ch, int W, int H, int m) {
        boolean rot = (cw > ch) != (W > H) && Math.abs(cw - ch) > 1;
        float w = rot ? ch : cw, h = rot ? cw : ch;
        float aw = W - 2f * m, ah = H - 2f * m;
        float s = Math.min(aw / w, ah / h);
        Matrix mx = new Matrix();
        if (rot) { mx.postRotate(90); mx.postTranslate(ch, 0); }
        mx.postScale(s, s);
        mx.postTranslate(m + (aw - w * s) / 2f, m + (ah - h * s) / 2f);
        return mx;
    }

    // ---------- PDF ----------
    public static final class PdfSource implements Source {
        private final ParcelFileDescriptor fd;
        private final PdfRenderer r;
        private final int margin;

        public PdfSource(File f, int margin) throws IOException {
            fd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY);
            r = new PdfRenderer(fd);
            this.margin = margin;
        }
        @Override public int count() { return r.getPageCount(); }
        @Override public Page open(int i) {
            final PdfRenderer.Page p = r.openPage(i);
            return new Page() {
                @Override public void draw(Bitmap strip, int y0, int W, int H) {
                    Matrix mx = fit(p.getWidth(), p.getHeight(), W, H, margin);
                    mx.postTranslate(0, -y0);
                    p.render(strip, null, mx, PdfRenderer.Page.RENDER_MODE_FOR_PRINT);
                }
                @Override public boolean photo() { return false; }
                @Override public void close() { p.close(); }
            };
        }
        @Override public void close() {
            try { r.close(); } catch (Exception ignored) {}
            try { fd.close(); } catch (Exception ignored) {}
        }
    }

    // ---------- Foto ----------
    public static final class ImageSource implements Source {
        private final Bitmap bmp;
        private final int margin;

        public ImageSource(File f, int margin) throws IOException {
            BitmapFactory.Options o = new BitmapFactory.Options();
            o.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(f.getPath(), o);
            int s = 1;
            while (Math.max(o.outWidth, o.outHeight) / s > 3600) s *= 2;
            o = new BitmapFactory.Options();
            o.inSampleSize = s;
            bmp = BitmapFactory.decodeFile(f.getPath(), o);
            if (bmp == null) throw new IOException("não consegui abrir a imagem");
            this.margin = margin;
        }
        @Override public int count() { return 1; }
        @Override public Page open(int i) {
            final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
            return new Page() {
                @Override public void draw(Bitmap strip, int y0, int W, int H) {
                    Matrix mx = fit(bmp.getWidth(), bmp.getHeight(), W, H, margin);
                    mx.postTranslate(0, -y0);
                    new Canvas(strip).drawBitmap(bmp, mx, paint);
                }
                @Override public boolean photo() { return true; }
                @Override public void close() {}
            };
        }
        @Override public void close() { bmp.recycle(); }
    }

    // Matriz de Bayer 4x4 (pontilhado ordenado, funciona faixa por faixa)
    private static final int[] BAYER = {8, 136, 40, 168, 200, 72, 232, 104, 56, 184, 24, 152, 248, 120, 216, 88};

    /** Escreve uma página PBM binária (P4): 1 = preto. */
    public static void writePbm(Page p, int W, int H, OutputStream os) throws IOException {
        os.write(("P4\n" + W + " " + H + "\n").getBytes("US-ASCII"));
        final int S = 256, bw = (W + 7) / 8;
        Bitmap strip = Bitmap.createBitmap(W, S, Bitmap.Config.ARGB_8888);
        int[] px = new int[W];
        byte[] row = new byte[bw];
        boolean photo = p.photo();
        try {
            for (int y0 = 0; y0 < H; y0 += S) {
                int rows = Math.min(S, H - y0);
                strip.eraseColor(Color.WHITE);
                p.draw(strip, y0, W, H);
                for (int r = 0; r < rows; r++) {
                    strip.getPixels(px, 0, W, 0, r, W, 1);
                    Arrays.fill(row, (byte) 0);
                    int y = y0 + r;
                    for (int x = 0; x < W; x++) {
                        int c = px[x], a = c >>> 24;
                        int R = (c >> 16) & 255, G = (c >> 8) & 255, B = c & 255;
                        if (a < 255) {
                            R = (R * a + 255 * (255 - a)) / 255;
                            G = (G * a + 255 * (255 - a)) / 255;
                            B = (B * a + 255 * (255 - a)) / 255;
                        }
                        int v = (R * 299 + G * 587 + B * 114) / 1000;
                        int t = photo ? BAYER[((y & 3) << 2) | (x & 3)] : 140;
                        if (v < t) row[x >> 3] |= (byte) (0x80 >> (x & 7));
                    }
                    os.write(row);
                }
            }
        } finally {
            strip.recycle();
        }
    }
}
