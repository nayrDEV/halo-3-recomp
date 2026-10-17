package org.h3recomp;

import android.app.NativeActivity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Toast;

import androidx.documentfile.provider.DocumentFile;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Slint launcher activity. It is a NativeActivity so Slint's Android backend
 * owns the window; the methods below are the small Java bridge the native
 * menu uses for the system file picker and for starting the SDL game.
 */
public class H3SlintActivity extends NativeActivity {
    private static final int REQUEST_FOLDER = 1001;
    private static final int REQUEST_ISO = 1002;

    static {
        System.loadLibrary("slintmenu");
    }

    private File gameRoot;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();

    private native void nativeInit(String root);
    private static native void nativeImportFinished(String path, String error);
    private static native void nativeExtractIso(String isoPath);

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

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            nativeImportFinished("", "Import cancelled.");
            return;
        }

        Uri uri = data.getData();
        worker.execute(() -> {
            try {
                if (requestCode == REQUEST_FOLDER) {
                    importFolder(uri);
                } else if (requestCode == REQUEST_ISO) {
                    importIso(uri);
                }
            } catch (Exception e) {
                nativeImportFinished("", "Import failed: " + e.getMessage());
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
