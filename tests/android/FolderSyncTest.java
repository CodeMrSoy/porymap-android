package org.codemrsoy.porymap;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public final class FolderSyncTest {
    static final class Memory implements FolderSync.Store {
        final Map<String, byte[]> data = new HashMap<>();
        int writes = 0;
        void put(String p, String value) { data.put(p, value.getBytes(StandardCharsets.UTF_8)); }
        public Set<String> files() { return new HashSet<>(data.keySet()); }
        public InputStream read(String path) { return new ByteArrayInputStream(data.get(path)); }
        public OutputStream write(String path) {
            writes++;
            return new ByteArrayOutputStream() { public void close() { data.put(path, toByteArray()); } };
        }
        public void delete(String path) { data.remove(path); }
        String value(String path) { return new String(data.get(path), StandardCharsets.UTF_8); }
    }
    static void require(boolean ok) { if (!ok) throw new AssertionError(); }
    static void put(File root, String p, String value) throws Exception {
        File file = new File(root, p); file.getParentFile().mkdirs(); Files.write(file.toPath(), value.getBytes(StandardCharsets.UTF_8));
    }
    public static void main(String[] args) throws Exception {
        File container = Files.createTempDirectory("porymap-folder-test").toFile();
        File root = new File(container, "project"), baseline = new File(container, "baseline");
        Memory remote = new Memory();
        remote.put("data/layouts/layouts.json", "{}"); remote.put("data/maps/map_groups.json", "{}");
        remote.put("data/maps/Test/map.bin", "original"); remote.put("README.md", "old");
        remote.put(".git/config", "secret git config");
        FolderSync.load(remote, root, baseline);
        require(!new File(root, ".git").exists());
        require(FolderSync.save(remote, root, baseline) == 0 && remote.writes == 0);
        // Changed map, unrelated external Git change and a new file.
        put(root, "data/maps/Test/map.bin", "painted"); put(root, "data/maps/New/map.json", "new map");
        remote.put("README.md", "external edit");
        require(FolderSync.save(remote, root, baseline) == 2);
        require(remote.value("data/maps/Test/map.bin").equals("painted"));
        require(remote.value("README.md").equals("external edit"));
        require(remote.value(".git/config").equals("secret git config"));
        // Conflict preflight must prevent every write, including alphabetically earlier files.
        put(root, "data/layouts/layouts.json", "changed layout");
        put(root, "data/maps/Test/map.bin", "local"); remote.put("data/maps/Test/map.bin", "external");
        int before = remote.writes;
        try { FolderSync.save(remote, root, baseline); throw new AssertionError("Conflict was ignored"); }
        catch (IOException expected) { require(expected.getMessage().contains("outside Porymap")); }
        require(remote.writes == before && remote.value("data/layouts/layouts.json").equals("{}"));
        // Matching remote content is safe after retrying a partially completed save.
        remote.put("data/maps/Test/map.bin", "local");
        require(FolderSync.save(remote, root, baseline) == 2);
        Files.delete(new File(root, "data/maps/New/map.json").toPath());
        require(FolderSync.save(remote, root, baseline) == 1 && !remote.data.containsKey("data/maps/New/map.json"));
        require(FolderSync.save(remote, root, baseline) == 0);
        try { FolderSync.allowed("../escape"); throw new AssertionError(); } catch (IOException expected) {}
        try { FolderSync.allowed("a/../../escape"); throw new AssertionError(); } catch (IOException expected) {}
        System.out.println("Folder sync: round-trip, unchanged files, Git preservation, conflicts, retries, create/delete and path checks passed");
    }
}
