package com.xlsmpdf.app;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * Entrega os PDFs gerados (guardados no cache do app) para o WhatsApp e outros apps,
 * sem precisar de bibliotecas extras.
 */
public class ArquivosProvider extends ContentProvider {
    public static final String AUTORIDADE = "com.xlsmpdf.app.arquivos";

    public static Uri uriPara(File base, File f) {
        String rel = f.getAbsolutePath().substring(base.getAbsolutePath().length());
        if (rel.startsWith("/")) rel = rel.substring(1);
        Uri.Builder b = new Uri.Builder().scheme("content").authority(AUTORIDADE);
        for (String parte : rel.split("/")) b.appendPath(parte);
        return b.build();
    }

    private File arquivo(Uri uri) throws FileNotFoundException {
        if (getContext() == null) throw new FileNotFoundException();
        File base = getContext().getCacheDir();
        StringBuilder rel = new StringBuilder();
        for (String p : uri.getPathSegments()) {
            if (rel.length() > 0) rel.append('/');
            rel.append(p);
        }
        File f = new File(base, rel.toString());
        try {
            if (!f.getCanonicalPath().startsWith(base.getCanonicalPath() + File.separator))
                throw new FileNotFoundException();
        } catch (IOException e) {
            throw new FileNotFoundException();
        }
        if (!f.isFile()) throw new FileNotFoundException();
        return f;
    }

    @Override public boolean onCreate() { return true; }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String sort) {
        File f;
        try { f = arquivo(uri); } catch (FileNotFoundException e) { return null; }
        if (projection == null) projection = new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        String[] cols = new String[projection.length];
        Object[] vals = new Object[projection.length];
        int n = 0;
        for (String c : projection) {
            if (OpenableColumns.DISPLAY_NAME.equals(c)) { cols[n] = c; vals[n++] = f.getName(); }
            else if (OpenableColumns.SIZE.equals(c)) { cols[n] = c; vals[n++] = f.length(); }
        }
        String[] c2 = new String[n];
        Object[] v2 = new Object[n];
        System.arraycopy(cols, 0, c2, 0, n);
        System.arraycopy(vals, 0, v2, 0, n);
        MatrixCursor m = new MatrixCursor(c2, 1);
        m.addRow(v2);
        return m;
    }

    @Override public String getType(Uri uri) { return "application/pdf"; }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(arquivo(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String s, String[] a) { return 0; }
    @Override public int update(Uri uri, ContentValues v, String s, String[] a) { return 0; }
}
