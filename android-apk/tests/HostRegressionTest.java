package org.libsdl.app;

import java.io.File;
import java.io.IOException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;

/** Run with the SDK android.jar and compiled app classes; no Android stubs are invoked. */
public final class HostRegressionTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static byte[] zip(String name, byte[] content) throws IOException {
        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
        try (java.util.zip.ZipOutputStream zip = new java.util.zip.ZipOutputStream(bytes)) {
            zip.putNextEntry(new java.util.zip.ZipEntry(name));
            zip.write(content);
            zip.closeEntry();
        }
        return bytes.toByteArray();
    }

    public static void main(String[] args) throws Exception {
        Path fixtures = Files.createTempDirectory(Path.of(args[2]), "regression-");
        String schema = Files.readString(Path.of(args[0]));
        Files.writeString(fixtures.resolve("normalized-schema.json"), ModSchemaJson.normalize(schema));
        String quoted = "{\"url\":\"https://example/a,} // /*\",\"escape\":\"\\\",]\",\"array\":[1, /* comment */ ],}";
        String normalized = ModSchemaJson.normalize("\uFEFF" + quoted);
        check(normalized.contains("https://example/a,} // /*"), "quoted URL was changed");
        check(normalized.contains("\\\",]"), "escaped quote was changed");
        check(normalized.length() == quoted.length() + 1, "error offsets shifted");
        check(!normalized.contains("/* comment */"), "comment was not stripped");
        try { ModSchemaJson.normalize("{ /* unfinished"); throw new AssertionError("bad comment accepted"); }
        catch (IOException expected) {
            check(expected instanceof LocalizedIOException &&
                "error_schema_comment".equals(((LocalizedIOException) expected).resource), "schema error has no translation key");
        }
        Files.writeString(fixtures.resolve("quoted-schema.json"), normalized);

        Path ini = fixtures.resolve("mod.ini");
        String original = Files.readString(Path.of(args[1]));
        Files.writeString(ini, original);
        LinkedHashMap<String, LinkedHashMap<String, String>> changes = new LinkedHashMap<>();
        LinkedHashMap<String, String> main = new LinkedHashMap<>();
        main.put("IncludeDir0", "Blue");
        changes.put("Main", main);
        Method patch = ModSettingsActivity.class.getDeclaredMethod("patchIni", File.class, LinkedHashMap.class);
        patch.setAccessible(true);
        patch.invoke(null, ini.toFile(), changes);
        String saved = Files.readString(ini);
        check(saved.replace("IncludeDir0=Blue", "IncludeDir0=\"Tails\"").replace("\r\n", "\n")
            .equals(original.replace("\r\n", "\n")), "unrelated mod metadata was changed");
        Method read = ModSettingsActivity.class.getDeclaredMethod("readIni", File.class);
        read.setAccessible(true);
        Map<?, ?> values = (Map<?, ?>) read.invoke(null, ini.toFile());
        check("Blue".equals(((Map<?, ?>) values.get("Main")).get("IncludeDir0")), "enum did not survive reload");

        Path source = Files.createDirectories(fixtures.resolve("old-install"));
        byte[] game = new byte[700000];
        for (int i = 0; i < game.length; i++) game[i] = (byte) (i * 31);
        Files.write(Files.createDirectories(source.resolve("game")).resolve("sample.bin"), game);
        Path config = Files.createDirectories(source.resolve(".config/UnleashedRecomp"));
        Files.writeString(config.resolve("android_mods_db.ini"), "[Mods]\nMod0=" + source.resolve("mods/Cast/mod.ini") + "\nOther=/other/mod.ini\n");
        Files.write(config.resolve("SYS-DATA"), new byte[] {1, 2, 3, 4});
        Path target = Files.createDirectories(fixtures.resolve("copied-install"));
        StorageMigration.Progress progress = new StorageMigration.Progress() {
            public boolean cancelled() { return false; }
            public void copied(long bytes) { }
        };
        StorageMigration.copy(source.toFile(), target.toFile(), progress);
        check(Arrays.equals(game, Files.readAllBytes(target.resolve("game/sample.bin"))), "game copy changed");
        check(Arrays.equals(new byte[] {1, 2, 3, 4}, Files.readAllBytes(target.resolve(".config/UnleashedRecomp/SYS-DATA"))), "save copy changed");
        String database = Files.readString(target.resolve(".config/UnleashedRecomp/android_mods_db.ini"));
        check(database.contains(target.resolve("mods/Cast/mod.ini").toString()) && database.contains("Other=/other/mod.ini"), "mod paths were not rebased correctly");
        check(Files.exists(source.resolve("game/sample.bin")), "original installation was removed");
        try { StorageMigration.copy(source.toFile(), target.toFile(), progress); throw new AssertionError("populated target overwritten"); }
        catch (IOException expected) {
            check(expected instanceof LocalizedIOException &&
                "error_copy_empty".equals(((LocalizedIOException) expected).resource), "copy error has no translation key");
        }

        Path cancelled = Files.createDirectories(fixtures.resolve("cancelled-install"));
        try { StorageMigration.copy(source.toFile(), cancelled.toFile(), new StorageMigration.Progress() {
            boolean stop;
            public boolean cancelled() { return stop; }
            public void copied(long bytes) { stop = true; }
        }); throw new AssertionError("cancellation was ignored"); }
        catch (IOException expected) {
            check(expected instanceof LocalizedIOException &&
                "error_copy_cancelled".equals(((LocalizedIOException) expected).resource), "cancellation has no translation key");
        }
        try (java.util.stream.Stream<Path> files = Files.list(cancelled)) {
            check(files.count() == 0, "cancelled copy was published");
        }
        check(Arrays.equals(game, Files.readAllBytes(source.resolve("game/sample.bin"))), "cancellation damaged the original");
        System.out.println("Schema strings/comments, mod.ini save/reload, verified migration, occupied target and cancellation: PASS");
        Path zipBatch = fixtures.resolve("zip-batch.part");
        byte[] dlc = "<DLC><Type>1</Type></DLC>".getBytes(StandardCharsets.UTF_8);
        int files = InstallerArchive.extract(new java.io.ByteArrayInputStream(zip("wrapper/dlc/Spagonia/DLC.xml", dlc)),
            zipBatch.toFile(), () -> false, bytes -> {});
        check(files == 1 && Arrays.equals(dlc, Files.readAllBytes(zipBatch.resolve("wrapper/dlc/Spagonia/DLC.xml"))), "DLC-only ZIP was not imported");
        Path badZip = fixtures.resolve("invalid-zip.part");
        try {
            InstallerArchive.extract(new java.io.ByteArrayInputStream(zip("../escaped.bin", dlc)),
                badZip.toFile(), () -> false, bytes -> {});
            throw new AssertionError("ZIP traversal accepted");
        } catch (IOException expected) {
            check(expected instanceof LocalizedIOException &&
                "error_zip_path".equals(((LocalizedIOException) expected).resource), "ZIP path error has no translation key");
        }
        check(!Files.exists(fixtures.resolve("escaped.bin")), "ZIP escaped staging");
        try {
            InstallerArchive.extract(new java.io.ByteArrayInputStream(zip("package.bin", game)),
                fixtures.resolve("cancelled-zip.part").toFile(), () -> true, bytes -> {});
            throw new AssertionError("ZIP cancellation ignored");
        } catch (IOException expected) {
            check(expected instanceof LocalizedIOException &&
                "error_zip_cancelled".equals(((LocalizedIOException) expected).resource), "ZIP cancellation has no translation key");
        }
        System.out.println("DLC ZIP wrapper extraction, traversal rejection and import cancellation: PASS");
        System.out.println("Fixtures: " + fixtures);
    }
}
