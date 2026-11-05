package org.codemrsoy.porymap;

import android.content.Context;
import android.net.Uri;
import org.json.JSONObject;
import java.io.*;

/** SAF only at import/export boundaries; the native editor uses real private paths. */
public final class AndroidFiles {
    private AndroidFiles() {}

    public static String dispatchTouch(Context context, String script, String resultPath) {
        try { return GestureProbe.dispatch(context, script, resultPath); }
        catch (Exception e) { return error(e); }
    }

    public static String renderLauncherIcon(Context context, String path, String unused) {
        try {
            android.graphics.drawable.Drawable icon = context.getPackageManager().getApplicationIcon(context.getApplicationInfo());
            android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(192, 192, android.graphics.Bitmap.Config.ARGB_8888);
            icon.setBounds(0, 0, 192, 192); icon.draw(new android.graphics.Canvas(bitmap));
            try (OutputStream out = new FileOutputStream(path)) { bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out); }
            boolean adaptive = icon instanceof android.graphics.drawable.AdaptiveIconDrawable;
            boolean themed = adaptive && android.os.Build.VERSION.SDK_INT >= 33 &&
                ((android.graphics.drawable.AdaptiveIconDrawable) icon).getMonochrome() != null;
            bitmap.recycle();
            return new JSONObject().put("adaptive", adaptive).put("monochrome", themed).toString();
        } catch (Exception e) { return error(e); }
    }

    public static String openFolder(Context context, String uri, String destination) {
        try { return AndroidFolder.open(context, uri, destination); }
        catch (Exception e) { return error(e); }
    }

    public static String saveFolder(Context context, String root, String unused) {
        try { return AndroidFolder.save(context, root); }
        catch (Exception e) { return error(e); }
    }

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
