package org.codemrsoy.porymap;

import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.*;
import java.util.*;
import org.json.JSONObject;

/** Persisted SAF tree access. Metadata lives outside the editable project. */
final class AndroidFolder implements FolderSync.Store {
    private final ContentResolver resolver;
    private final Uri tree;
    private final Map<String, Uri> files = new HashMap<>(), dirs = new HashMap<>();
    AndroidFolder(Context context, String location) throws Exception {
        resolver = context.getContentResolver(); tree = Uri.parse(location);
        if (!DocumentsContract.isTreeUri(tree)) throw new IOException("Choose a folder using Android's folder picker");
        resolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        dirs.put("", DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree)));
    }
    private void scan(String prefix, Uri dir, int depth) throws Exception {
        if (depth > 64) throw new IOException("Project folders are nested too deeply");
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, DocumentsContract.getDocumentId(dir));
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot list selected folder");
            while (cursor.moveToNext()) {
                String name = cursor.getString(1);
                if (name == null || name.contains("/") || name.contains("\\") || name.equals(".") || name.equals(".."))
                    throw new IOException("Invalid name in selected folder");
                String path = prefix.isEmpty() ? name : prefix + "/" + name;
                if (!FolderSync.allowed(path)) continue;
                if (files.containsKey(path) || dirs.containsKey(path)) throw new IOException("Duplicate project path: " + path);
                Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0));
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))) { dirs.put(path, uri); scan(path, uri, depth + 1); }
                else files.put(path, uri);
                if (files.size() + dirs.size() > 100000) throw new IOException("Too many project entries");
            }
        }
    }
    public Set<String> files() throws Exception {
        files.clear(); Uri root = dirs.get(""); dirs.clear(); dirs.put("", root); scan("", root, 0);
        return new HashSet<>(files.keySet());
    }
    public InputStream read(String path) throws Exception { return resolver.openInputStream(files.get(path)); }
    private Uri directory(String path) throws Exception {
        if (dirs.containsKey(path)) return dirs.get(path);
        int slash = path.lastIndexOf('/');
        Uri parent = directory(slash < 0 ? "" : path.substring(0, slash));
        Uri created = DocumentsContract.createDocument(resolver, parent, DocumentsContract.Document.MIME_TYPE_DIR, path.substring(slash + 1));
        if (created == null) throw new IOException("Cannot create project directory: " + path);
        dirs.put(path, created); return created;
    }
    public OutputStream write(String path) throws Exception {
        Uri uri = files.get(path);
        if (uri == null) {
            int slash = path.lastIndexOf('/');
            uri = DocumentsContract.createDocument(resolver, directory(slash < 0 ? "" : path.substring(0, slash)),
                "application/octet-stream", path.substring(slash + 1));
            if (uri == null) throw new IOException("Cannot create file: " + path);
            files.put(path, uri);
        }
        return resolver.openOutputStream(uri, "wt");
    }
    public void delete(String path) throws Exception {
        if (!DocumentsContract.deleteDocument(resolver, files.get(path))) throw new IOException("Cannot delete: " + path);
    }
    static String open(Context context, String uri, String destination) throws Exception {
        File container = new File(destination), root = new File(container, "project");
        if (!container.mkdirs()) throw new IOException("Cannot create project workspace");
        FolderSync.load(new AndroidFolder(context, uri), root, new File(container, "baseline.properties"));
        Properties link = new Properties(); link.setProperty("uri", uri);
        FolderSync.checkpoint(new File(container, "folder-link.properties"), link);
        return new JSONObject().put("root", root.getAbsolutePath()).toString();
    }
    static String save(Context context, String rootPath) throws Exception {
        File root = new File(rootPath), container = root.getParentFile();
        File linkFile = new File(container, "folder-link.properties");
        if (!linkFile.isFile()) return new JSONObject().put("linked", false).toString();
        Properties link = new Properties(); try (InputStream in = new FileInputStream(linkFile)) { link.load(in); }
        int count = FolderSync.save(new AndroidFolder(context, link.getProperty("uri")), root, new File(container, "baseline.properties"));
        return new JSONObject().put("linked", true).put("files", count).toString();
    }
}
