package org.codemrsoy.porymap;

import java.io.*;
import java.nio.file.Files;
import java.util.*;
import java.util.zip.*;

/** Project bytes stay unchanged. ZIPs are expanded only into a new app-owned folder. */
public final class ProjectArchive {
    private ProjectArchive() {}
    private static final long MIB = 1024L * 1024L;
    private static final OutputStream DISCARD = new OutputStream() {
        @Override public void write(int value) {}
        @Override public void write(byte[] value, int offset, int count) {}
    };

    public static File importZip(InputStream input, File destination) throws IOException {
        return importZip(input, destination, 1024 * MIB, 128 * MIB, 100000);
    }

    static File importZip(InputStream input, File destination, long maxTotal,
                          long maxFile, int maxEntries) throws IOException {
        if (destination.exists()) throw new IOException("Import destination already exists");
        if (!destination.mkdirs()) throw new IOException("Cannot create import folder");
        String prefix = destination.getCanonicalPath() + File.separator;
        Set<String> names = new HashSet<>();
        long total = 0;
        int count = 0;
        byte[] buffer = new byte[65536];
        boolean complete = false;
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(input))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (++count > maxEntries) throw new IOException("Too many archive entries");
                String name = entry.getName().replace('\\', '/');
                if (name.isEmpty() || name.startsWith("/") || name.matches("^[A-Za-z]:.*"))
                    throw new IOException("Absolute archive path rejected");
                for (String part : name.split("/")) {
                    if (part.equals("..") || part.equals("."))
                        throw new IOException("Unsafe archive path: " + name);
                }
                File target = new File(destination, name).getCanonicalFile();
                if (!target.getPath().startsWith(prefix)) throw new IOException("Archive path escapes project");
                if (!names.add(target.getPath())) throw new IOException("Duplicate archive entry: " + name);
                boolean skip = excluded(name);
                if (entry.isDirectory()) {
                    if (!skip && !target.isDirectory() && !target.mkdirs()) throw new IOException("Cannot create folder: " + name);
                    if (zip.read() != -1) throw new IOException("Directory entry contains file data");
                } else {
                    if (entry.getSize() > maxFile) throw new IOException("Archive file is too large: " + name);
                    File parent = target.getParentFile();
                    if (!skip && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create parent folder");
                    long size = 0;
                    try (OutputStream out = skip ? DISCARD : new BufferedOutputStream(new FileOutputStream(target))) {
                        int n;
                        while ((n = zip.read(buffer)) != -1) {
                            size += n; total += n;
                            if (size > maxFile || total > maxTotal)
                                throw new IOException("Archive exceeds project size limits");
                            out.write(buffer, 0, n);
                        }
                    }
                }
                zip.closeEntry(); // Also verifies the entry CRC.
            }
            List<File> roots = new ArrayList<>();
            findRoots(destination, 0, roots);
            if (roots.size() != 1) throw new IOException("ZIP must contain exactly one Porymap project (data/maps and data/layouts)");
            complete = true;
            return roots.get(0);
        } finally {
            if (!complete) removeImport(destination);
        }
    }

    public static boolean isProject(File folder) {
        return new File(folder, "data/layouts/layouts.json").isFile()
            && new File(folder, "data/maps/map_groups.json").isFile();
    }

    static void findRoots(File folder, int depth, List<File> roots) throws IOException {
        if (isProject(folder)) { roots.add(folder); return; }
        if (depth >= 3) return;
        File[] files = folder.listFiles();
        if (files == null) throw new IOException("Cannot list imported folders");
        for (File f : files) if (f.isDirectory()) findRoots(f, depth + 1, roots);
    }

    private static boolean excluded(String name) {
        for (String part : name.split("/")) {
            if (part.equals(".git") || part.equals("build") || part.equals(".gradle")
                || part.equals(".cxx") || part.equals("__pycache__") || part.equals("node_modules")) return true;
        }
        return false;
    }

    public static void exportZip(File project, OutputStream output) throws IOException {
        if (!isProject(project)) throw new IOException("Not a Porymap project");
        try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(output))) {
            addDirectory(project.getCanonicalFile(), project.getCanonicalFile(), zip);
        }
    }

    private static void addDirectory(File root, File folder, ZipOutputStream zip) throws IOException {
        File[] files = folder.listFiles();
        if (files == null) throw new IOException("Cannot read project folder");
        Arrays.sort(files, Comparator.comparing(File::getName));
        byte[] buffer = new byte[65536];
        for (File file : files) {
            String relative = root.toPath().relativize(file.toPath()).toString().replace(File.separatorChar, '/');
            if (excluded(relative) || Files.isSymbolicLink(file.toPath())) continue;
            if (!file.getCanonicalPath().startsWith(root.getPath() + File.separator))
                throw new IOException("Project path escapes root");
            if (file.isDirectory()) {
                zip.putNextEntry(new ZipEntry(relative + "/")); zip.closeEntry();
                addDirectory(root, file, zip);
            } else {
                zip.putNextEntry(new ZipEntry(relative));
                try (InputStream in = new BufferedInputStream(new FileInputStream(file))) {
                    int n;
                    while ((n = in.read(buffer)) != -1) zip.write(buffer, 0, n);
                }
                zip.closeEntry();
            }
        }
    }

    private static void removeImport(File folder) {
        // This method is used only for the fresh destination created by importZip.
        File[] files = folder.listFiles();
        if (files != null) for (File f : files) {
            if (f.isDirectory()) removeImport(f); else f.delete();
        }
        folder.delete();
    }
}
