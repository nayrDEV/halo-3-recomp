package org.h3recomp;

import android.app.NativeActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Toast;

import androidx.documentfile.provider.DocumentFile;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Slint launcher activity. It is a NativeActivity so Slint's Android backend
 * owns the window; the methods below are the small Java bridge the native
 * menu uses for the system file picker and for starting the SDL game.
 */
public class H3SlintActivity extends NativeActivity {
    private static final int REQUEST_FOLDER = 1001;
    private static final int REQUEST_ISO = 1002;
    private static final int REQUEST_EXPORT_SAVES = 1003;
    private static final int REQUEST_IMPORT_SAVES = 1004;
    private static final int REQUEST_EXPORT_LOG = 1005;

    static {
        System.loadLibrary("slintmenu");
    }

    private File gameRoot;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();

    private native void nativeInit(String root);
    private static native void nativeImportFinished(String path, String error);
    private static native void nativeExtractIso(String isoPath);
    private static native void nativeSavesExported(String path, String error);
    private static native void nativeSavesImported(String path, String error);
    private static native void nativeLogExported(String path, String error);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Everything (game data, ASTC texture cache, saves) lives in the
        // app's private internal storage; no external-storage permission is
        // needed and the files move with the app.
        File base = getFilesDir();
        gameRoot = new File(base, "Halo3");
        gameRoot.mkdirs();

        // Slint starts slint_main from super.onCreate(); give it the path first.
        nativeInit(gameRoot.getAbsolutePath());
        super.onCreate(savedInstanceState);
    }

    public void pickFolder() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_FOLDER);
        });
    }

    public void pickIso() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_ISO);
        });
    }

    public void openFolder(String path) {
        runOnUiThread(() -> Toast.makeText(this, path, Toast.LENGTH_LONG).show());
    }

    public void startGame() {
        android.util.Log.i("H3Recomp", "startGame requested");
        runOnUiThread(() -> {
            try {
                android.util.Log.i("H3Recomp", "launching H3GameActivity");
                startActivity(new Intent(this, H3GameActivity.class));
            } catch (Exception e) {
                android.util.Log.e("H3Recomp", "game launch failed", e);
                Toast.makeText(this, "Launch failed: " + e, Toast.LENGTH_LONG).show();
            }
        });
    }

    public void exportSaves() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/zip");
            intent.putExtra(Intent.EXTRA_TITLE, "Halo3-saves.zip");
            startActivityForResult(intent, REQUEST_EXPORT_SAVES);
        });
    }

    public void exportLog() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("text/plain");
            intent.putExtra(Intent.EXTRA_TITLE, "Halo3Recomp-log.txt");
            startActivityForResult(intent, REQUEST_EXPORT_LOG);
        });
    }

    public void importSaves() {
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_IMPORT_SAVES);
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            if (requestCode == REQUEST_EXPORT_SAVES) {
                nativeSavesExported("", "Export cancelled.");
            } else if (requestCode == REQUEST_IMPORT_SAVES) {
                nativeSavesImported("", "Import cancelled.");
            } else if (requestCode == REQUEST_EXPORT_LOG) {
                nativeLogExported("", "Log export cancelled.");
            } else {
                nativeImportFinished("", "Import cancelled.");
            }
            return;
        }

        Uri uri = data.getData();
        worker.execute(() -> {
            try {
                if (requestCode == REQUEST_FOLDER) {
                    importFolder(uri);
                } else if (requestCode == REQUEST_ISO) {
                    importIso(uri);
                } else if (requestCode == REQUEST_EXPORT_SAVES) {
                    exportSaves(uri);
                } else if (requestCode == REQUEST_IMPORT_SAVES) {
                    importSaves(uri);
                } else if (requestCode == REQUEST_EXPORT_LOG) {
                    exportLog(uri);
                }
            } catch (Exception e) {
                if (requestCode == REQUEST_EXPORT_SAVES) {
                    nativeSavesExported("", "Save export failed: " + e.getMessage());
                } else if (requestCode == REQUEST_IMPORT_SAVES) {
                    nativeSavesImported("", "Save import failed: " + e.getMessage());
                } else if (requestCode == REQUEST_EXPORT_LOG) {
                    nativeLogExported("", "Log export failed: " + e.getMessage());
                } else {
                    nativeImportFinished("", "Import failed: " + e.getMessage());
                }
            }
        });
    }

    private void importFolder(Uri treeUri) throws Exception {
        DocumentFile source = DocumentFile.fromTreeUri(this, treeUri);
        if (source == null || !source.isDirectory()) {
            nativeImportFinished("", "The selected folder can't be read.");
            return;
        }

        File gameDir = new File(gameRoot, "game");
        gameDir.mkdirs();

        // Accept either a folder containing default.xex, or its parent with
        // a single "game" child. The common case is the extracted disc root.
        DocumentFile contents = source;
        DocumentFile nestedGame = source.findFile("game");
        if (source.findFile("default.xex") == null && nestedGame != null && nestedGame.isDirectory()) {
            contents = nestedGame;
            gameDir = new File(gameRoot, "game");
            gameDir.mkdirs();
        }

        copyTree(contents, gameDir);

        File xex = new File(gameDir, "default.xex");
        if (!xex.isFile()) {
            nativeImportFinished("", "No default.xex found in the selected folder.");
            return;
        }
        nativeImportFinished(gameRoot.getAbsolutePath(), "");
    }

    private void importIso(Uri isoUri) throws Exception {
        File iso = new File(gameRoot, "import.iso");
        try (InputStream in = getContentResolver().openInputStream(isoUri);
             OutputStream out = new FileOutputStream(iso)) {
            if (in == null) {
                nativeImportFinished("", "The selected ISO can't be read.");
                return;
            }
            byte[] buffer = new byte[1024 * 1024];
            int count;
            while ((count = in.read(buffer)) > 0) {
                out.write(buffer, 0, count);
            }
        }
        nativeExtractIso(iso.getAbsolutePath());
    }

    private void exportLog(Uri destination) throws Exception {
        StringBuilder text = new StringBuilder();
        text.append("Halo 3 Recompiled Android log\n");
        text.append("Device: ").append(android.os.Build.MANUFACTURER).append(" ")
            .append(android.os.Build.MODEL).append("\n");
        text.append("Android: ").append(android.os.Build.VERSION.RELEASE)
            .append(" / API ").append(android.os.Build.VERSION.SDK_INT).append("\n");
        text.append("ABIs: ").append(String.join(", ", android.os.Build.SUPPORTED_ABIS)).append("\n\n");

        File log = new File(gameRoot, "h3recomp.log");
        if (log.isFile()) {
            try (InputStream in = new BufferedInputStream(new FileInputStream(log))) {
                appendLimited(text, in, 4 * 1024 * 1024);
            }
        } else {
            text.append("No h3recomp.log was written. Is 'WRITE LOG TO FILE' enabled before Play?\n");
        }

        text.append("\n\n--- logcat (last 3000 lines) ---\n");
        try {
            Process process = new ProcessBuilder("logcat", "-d", "-v", "threadtime", "-t", "3000")
                .redirectErrorStream(true).start();
            try (InputStream in = new BufferedInputStream(process.getInputStream())) {
                appendLimited(text, in, 4 * 1024 * 1024);
            }
            process.waitFor();
        } catch (Exception e) {
            text.append("logcat unavailable: ").append(e).append("\n");
        }

        try (OutputStream out = getContentResolver().openOutputStream(destination)) {
            if (out == null) {
                nativeLogExported("", "Can't open the selected log file.");
                return;
            }
            out.write(text.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
        }
        nativeLogExported("", "");
    }

    private void appendLimited(StringBuilder text, InputStream in, int limit) throws Exception {
        byte[] buffer = new byte[65536];
        int total = 0;
        int count;
        while (total < limit && (count = in.read(buffer)) > 0) {
            int keep = Math.min(count, limit - total);
            text.append(new String(buffer, 0, keep, java.nio.charset.StandardCharsets.UTF_8));
            total += keep;
        }
    }

    private void exportSaves(Uri destination) throws Exception {
        OutputStream raw = getContentResolver().openOutputStream(destination);
        if (raw == null) {
            nativeSavesExported("", "Can't open the selected save file.");
            return;
        }
        try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(raw))) {
            File hdd = new File(gameRoot, "hdd");
            zipDirectory(hdd, "hdd", zip);
            File settings = new File(gameRoot, "settings.ini");
            if (settings.isFile()) {
                zip.putNextEntry(new ZipEntry("settings.ini"));
                try (InputStream in = new FileInputStream(settings)) {
                    copyStream(in, zip);
                }
                zip.closeEntry();
            }
        }
        nativeSavesExported("", "");
    }

    private void zipDirectory(File directory, String prefix, ZipOutputStream zip) throws Exception {
        File[] files = directory.listFiles();
        if (files == null) {
            return;
        }
        for (File file : files) {
            String name = prefix + "/" + file.getName();
            if (file.isDirectory()) {
                zip.putNextEntry(new ZipEntry(name + "/"));
                zip.closeEntry();
                zipDirectory(file, name, zip);
            } else {
                zip.putNextEntry(new ZipEntry(name));
                try (InputStream in = new BufferedInputStream(new FileInputStream(file))) {
                    copyStream(in, zip);
                }
                zip.closeEntry();
            }
        }
    }

    private void copyStream(InputStream in, OutputStream out) throws Exception {
        byte[] buffer = new byte[1024 * 1024];
        int count;
        while ((count = in.read(buffer)) > 0) {
            out.write(buffer, 0, count);
        }
    }

    private void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }

    private void importSaves(Uri source) throws Exception {
        File staging = new File(getCacheDir(), "h3-saves-import");
        deleteTree(staging);
        if (!staging.mkdirs()) {
            nativeSavesImported("", "Can't create the save staging folder.");
            return;
        }

        InputStream raw = getContentResolver().openInputStream(source);
        if (raw == null) {
            nativeSavesImported("", "Can't open the selected save archive.");
            return;
        }
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(raw))) {
            String canonicalRoot = staging.getCanonicalPath() + File.separator;
            ZipEntry entry;
            byte[] buffer = new byte[1024 * 1024];
            while ((entry = zip.getNextEntry()) != null) {
                String name = entry.getName().replace('\\', '/');
                if (name.isEmpty() || name.startsWith("/") || name.contains("..")) {
                    continue;
                }
                File output = new File(staging, name);
                if (!output.getCanonicalPath().startsWith(canonicalRoot)) {
                    continue;
                }
                if (entry.isDirectory()) {
                    output.mkdirs();
                } else {
                    output.getParentFile().mkdirs();
                    try (OutputStream out = new BufferedOutputStream(new FileOutputStream(output))) {
                        int count;
                        while ((count = zip.read(buffer)) > 0) {
                            out.write(buffer, 0, count);
                        }
                    }
                }
            }
        }

        File sourceRoot = new File(staging, "hdd");
        if (sourceRoot.isDirectory() && sourceRoot.listFiles() != null) {
            // The export puts settings.ini next to hdd/; restore it too.
            File importedSettings = new File(staging, "settings.ini");
            if (importedSettings.isFile()) {
                File settings = new File(gameRoot, "settings.ini");
                try (InputStream in = new BufferedInputStream(new FileInputStream(importedSettings));
                     OutputStream out = new BufferedOutputStream(new FileOutputStream(settings))) {
                    copyStream(in, out);
                }
            }
        } else {
            sourceRoot = staging;
        }
        File[] contents = sourceRoot.listFiles();
        if (contents == null || contents.length == 0) {
            nativeSavesImported("", "The archive contains no save files.");
            return;
        }

        File hdd = new File(gameRoot, "hdd");
        File backup = new File(gameRoot, "hdd.bak");
        deleteTree(backup);
        if (hdd.exists() && !hdd.renameTo(backup)) {
            nativeSavesImported("", "Can't back up the current saves.");
            return;
        }
        if (!sourceRoot.renameTo(hdd)) {
            // Cache and files directories can be on different mounts.
            hdd.mkdirs();
            copyFileTree(sourceRoot, hdd);
            deleteTree(sourceRoot);
        }
        deleteTree(staging);
        nativeSavesImported("", "");
    }

    private void copyFileTree(File source, File target) throws Exception {
        if (source.isDirectory()) {
            target.mkdirs();
            File[] children = source.listFiles();
            if (children != null) {
                for (File child : children) {
                    copyFileTree(child, new File(target, child.getName()));
                }
            }
        } else {
            try (InputStream in = new BufferedInputStream(new FileInputStream(source));
                 OutputStream out = new BufferedOutputStream(new FileOutputStream(target))) {
                copyStream(in, out);
            }
        }
    }

    private void copyTree(DocumentFile source, File target) throws Exception {
        DocumentFile[] children = source.listFiles();
        for (DocumentFile child : children) {
            String name = child.getName();
            if (name == null || name.equals(".") || name.equals("..")
                    || name.contains("/") || name.contains("\\")) {
                continue;
            }
            File destination = new File(target, name);
            if (child.isDirectory()) {
                destination.mkdirs();
                copyTree(child, destination);
            } else {
                try (InputStream in = getContentResolver().openInputStream(child.getUri());
                     OutputStream out = new FileOutputStream(destination)) {
                    if (in == null) {
                        continue;
                    }
                    byte[] buffer = new byte[1024 * 1024];
                    int count;
                    while ((count = in.read(buffer)) > 0) {
                        out.write(buffer, 0, count);
                    }
                }
            }
        }
    }

    @Override
    protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }
}
