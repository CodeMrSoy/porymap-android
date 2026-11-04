package org.codemrsoy.porymap;

import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

public final class ProjectArchiveTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static byte[] zip(Map<String, byte[]> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (Map.Entry<String, byte[]> item : entries.entrySet()) {
                zip.putNextEntry(new ZipEntry(item.getKey()));
                zip.write(item.getValue()); zip.closeEntry();
            }
        }
        return bytes.toByteArray();
    }
    private static void reject(File base, String name, Map<String, byte[]> entries) throws IOException {
        File destination = new File(base, name);
        try {
            ProjectArchive.importZip(new ByteArrayInputStream(zip(entries)), destination);
            throw new AssertionError("Unsafe archive accepted: " + name);
        } catch (IOException expected) {
            check(!destination.exists(), "Failed import was not removed: " + name);
        }
    }
    public static void main(String[] args) throws Exception {
        File base = Files.createTempDirectory("porymap-archive-test").toFile();
        Map<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("game/data/layouts/layouts.json", "{\"layouts\":[]}".getBytes("UTF-8"));
        entries.put("game/data/maps/map_groups.json", "{}".getBytes("UTF-8"));
        byte[] map = new byte[] {0, 48, 21, 49, (byte)255, 63, 0, 0};
        byte[] graphic = new byte[4096]; new Random(34).nextBytes(graphic);
        entries.put("game/data/layouts/Test/map.bin", map);
        entries.put("game/data/tilesets/test/tiles.png", graphic);
        entries.put("game/data/maps/Test/map.json", "{\"warp_events\":[{\"x\":2,\"y\":4}]}".getBytes("UTF-8"));
        entries.put("game/.git/config", new byte[] {42});
        entries.put("game/build/temporary.o", new byte[] {43});
        File project = ProjectArchive.importZip(new ByteArrayInputStream(zip(entries)), new File(base, "first"));
        check(!new File(project, ".git").exists(), "Git metadata was imported");
        check(!new File(project, "build").exists(), "Build output was imported");
        check(Arrays.equals(map, Files.readAllBytes(new File(project, "data/layouts/Test/map.bin").toPath())), "Map bytes changed on import");
        byte[] edited = map.clone(); edited[1] = 52;
        Files.write(new File(project, "data/layouts/Test/map.bin").toPath(), edited);
        ByteArrayOutputStream exported = new ByteArrayOutputStream();
        ProjectArchive.exportZip(project, exported);
        File reopened = ProjectArchive.importZip(new ByteArrayInputStream(exported.toByteArray()), new File(base, "second"));
        for (Map.Entry<String, byte[]> item : entries.entrySet()) {
            String path = item.getKey().substring(5);
            if (path.startsWith(".git/") || path.startsWith("build/")) continue;
            byte[] expected = path.endsWith("map.bin") ? edited : item.getValue();
            check(Arrays.equals(expected, Files.readAllBytes(new File(reopened, path).toPath())), "Round trip changed " + path);
        }
        for (String bad : new String[] {"../outside.txt", "/outside.txt", "C:/outside.txt", "game/../../outside.txt", "game\\..\\outside.txt"}) {
            Map<String, byte[]> unsafe = new LinkedHashMap<>(entries);
            unsafe.put(bad, new byte[] {1}); reject(base, "reject-" + Math.abs(bad.hashCode()), unsafe);
        }
        reject(base, "no-project", Collections.singletonMap("nothing.txt", new byte[] {1}));
        Map<String, byte[]> multiple = new LinkedHashMap<>(entries);
        multiple.put("second/data/layouts/layouts.json", new byte[] {1});
        multiple.put("second/data/maps/map_groups.json", new byte[] {1});
        reject(base, "multiple", multiple);
        File limited = new File(base, "limited");
        try {
            ProjectArchive.importZip(new ByteArrayInputStream(zip(entries)), limited, 32, 16, 100);
            throw new AssertionError("Size limit was ignored");
        } catch (IOException expected) { check(!limited.exists(), "Partial oversized import survived"); }
        check(!new File(base, "outside.txt").exists(), "Archive escaped extraction root");
        try {
            ProjectArchive.importZip(new ByteArrayInputStream(zip(entries)), new File(base, "first"));
            throw new AssertionError("Existing project overwritten");
        } catch (IOException expected) { check(project.exists(), "Existing project damaged"); }
        System.out.println("PASS: binary/map/event round trips, traversal rejection, size limits, failed-import cleanup and no overwrite");
    }
}
