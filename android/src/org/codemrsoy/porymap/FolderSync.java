package org.codemrsoy.porymap;

import java.io.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;

/** Provider-independent mirror and conflict checks. Never touches .git or build caches. */
public final class FolderSync {
    public interface Store {
        Set<String> files() throws Exception;
        InputStream read(String path) throws Exception;
        OutputStream write(String path) throws Exception;
        void delete(String path) throws Exception;
    }
    private static final Set<String> SKIP = new HashSet<>(Arrays.asList(
        ".git", "build", ".gradle", ".cxx", "__pycache__", "node_modules"));
    static boolean allowed(String path) throws IOException {
        if (path.startsWith("/") || path.contains("\\")) throw new IOException("Invalid project path: " + path);
        for (String part : path.split("/", -1)) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) throw new IOException("Invalid project path: " + path);
            if (SKIP.contains(part)) return false;
        }
        return true;
    }
    static String digest(InputStream stream) throws Exception {
        if (stream == null) throw new IOException("Cannot read project file");
        MessageDigest hash = MessageDigest.getInstance("SHA-256");
        try (InputStream in = stream) {
            byte[] buf = new byte[65536]; int n; long size = 0;
            while ((n = in.read(buf)) != -1) {
                if ((size += n) > 128L * 1024 * 1024) throw new IOException("Project file exceeds 128 MiB");
                hash.update(buf, 0, n);
            }
        }
        StringBuilder out = new StringBuilder();
        for (byte b : hash.digest()) out.append(String.format(Locale.ROOT, "%02x", b & 255));
        return out.toString();
    }
    private static void copy(InputStream stream, OutputStream target, long[] total) throws Exception {
        if (stream == null || target == null) throw new IOException("Cannot access project file");
        try (InputStream in = stream; OutputStream out = target) {
            byte[] buf = new byte[65536]; int n; long size = 0;
            while ((n = in.read(buf)) != -1) {
                if ((size += n) > 128L * 1024 * 1024 || (total[0] += n) > 1024L * 1024 * 1024)
                    throw new IOException("Project exceeds import/save size limit");
                out.write(buf, 0, n);
            }
        }
    }
    static void checkpoint(File file, Properties state) throws Exception {
        File temp = new File(file.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(temp)) { state.store(out, "Porymap folder baseline"); out.getFD().sync(); }
        Files.move(temp.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
    }
    public static void load(Store source, File root, File baseline) throws Exception {
        if (root.exists()) throw new IOException("Working folder already exists");
        if (!root.mkdirs()) throw new IOException("Cannot create working folder");
        Set<String> paths = source.files();
        if (!paths.contains("data/layouts/layouts.json") || !paths.contains("data/maps/map_groups.json"))
            throw new IOException("Select the project root folder containing data/layouts and data/maps.");
        Properties state = new Properties(); long[] total = {0};
        for (String path : new TreeSet<>(paths)) {
            if (!allowed(path)) continue;
            File file = new File(root, path); file.getParentFile().mkdirs();
            copy(source.read(path), new FileOutputStream(file), total);
            state.setProperty(path, digest(new FileInputStream(file)));
        }
        checkpoint(baseline, state);
    }
    private static Map<String, File> localFiles(File root) throws Exception {
        Map<String, File> result = new TreeMap<>();
        try (java.util.stream.Stream<Path> paths = Files.walk(root.toPath())) {
            Iterator<Path> it = paths.iterator();
            while (it.hasNext()) {
                Path path = it.next();
                if (Files.isSymbolicLink(path)) throw new IOException("Linked files are not supported");
                if (!Files.isRegularFile(path)) continue;
                String relative = root.toPath().relativize(path).toString().replace(File.separatorChar, '/');
                if (allowed(relative)) result.put(relative, path.toFile());
                if (result.size() > 100000) throw new IOException("Too many project files");
            }
        }
        return result;
    }
    public static int save(Store destination, File root, File baseline) throws Exception {
        Properties before = new Properties();
        try (InputStream in = new FileInputStream(baseline)) { before.load(in); }
        Map<String, File> local = localFiles(root);
        Map<String, String> changed = new TreeMap<>();
        Set<String> all = new TreeSet<>(before.stringPropertyNames()); all.addAll(local.keySet());
        for (String path : all) {
            String hash = local.containsKey(path) ? digest(new FileInputStream(local.get(path))) : null;
            if (!Objects.equals(before.getProperty(path), hash)) changed.put(path, hash);
        }
        if (changed.isEmpty()) return 0;
        Set<String> remote = destination.files();
        // Preflight every changed file before writing any of them. Unrelated Git edits survive.
        for (Map.Entry<String, String> entry : changed.entrySet()) {
            String path = entry.getKey();
            String current = remote.contains(path) ? digest(destination.read(path)) : null;
            if (!Objects.equals(current, before.getProperty(path)) && !Objects.equals(current, entry.getValue()))
                throw new IOException("Folder changed outside Porymap: " + path + ". Your edits are safe on this device. Export a ZIP before reopening the folder to resolve the conflict.");
        }
        long[] total = {0}; int count = 0;
        for (Map.Entry<String, String> entry : changed.entrySet()) {
            String path = entry.getKey(), hash = entry.getValue();
            if (hash == null) { if (remote.contains(path)) destination.delete(path); before.remove(path); }
            else { copy(new FileInputStream(local.get(path)), destination.write(path), total); before.setProperty(path, hash); }
            // Keep progress if a provider disconnects halfway through a save.
            checkpoint(baseline, before); count++;
        }
        return count;
    }
}
