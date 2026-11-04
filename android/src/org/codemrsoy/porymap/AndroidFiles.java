package org.codemrsoy.porymap;

import android.content.Context;
import android.net.Uri;
import org.json.JSONObject;
import java.io.*;

/** SAF only at import/export boundaries; the native editor uses real private paths. */
public final class AndroidFiles {
    private AndroidFiles() {}

    public static String importProject(Context context, String location, String destination) {
        try {
            Uri uri = Uri.parse(location);
            InputStream stream = "content".equals(uri.getScheme())
                ? context.getContentResolver().openInputStream(uri)
                : new FileInputStream("file".equals(uri.getScheme()) ? uri.getPath() : location);
            if (stream == null) throw new IOException("Cannot read selected ZIP");
            File root;
            try (InputStream input = stream) {
                root = ProjectArchive.importZip(input, new File(destination));
            }
            return new JSONObject().put("root", root.getAbsolutePath()).toString();
        } catch (Exception e) {
            return error(e);
        }
    }

    public static String exportProject(Context context, String root, String location) {
        try {
            Uri uri = Uri.parse(location);
            OutputStream stream = "content".equals(uri.getScheme())
                ? context.getContentResolver().openOutputStream(uri, "wt")
                : new FileOutputStream("file".equals(uri.getScheme()) ? uri.getPath() : location);
            if (stream == null) throw new IOException("Cannot write selected ZIP");
            try (OutputStream output = stream) {
                ProjectArchive.exportZip(new File(root), output);
            }
            return new JSONObject().put("ok", true).toString();
        } catch (Exception e) {
            return error(e);
        }
    }

    private static String error(Exception e) {
        try { return new JSONObject().put("error", e.toString()).toString(); }
        catch (Exception impossible) { return "{\"error\":\"Project operation failed\"}"; }
    }
}
