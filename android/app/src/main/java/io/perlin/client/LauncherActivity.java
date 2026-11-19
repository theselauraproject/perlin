package io.perlin.client;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;

public class LauncherActivity extends Activity {
    private static final int REQUEST_LEGACY = 1;
    private boolean waitingForSettings = false;
    private boolean started = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (PerlinActivity.alive) {
            launch();
            return;
        }
        proceed();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (waitingForSettings) {
            waitingForSettings = false;
            proceed();
        }
    }

    private void ask() {
        new AlertDialog.Builder(this)
                .setTitle("Perlin needs storage access")
                .setMessage("Perlin keeps its worlds, texture packs, shader packs, screenshots, logs and settings in "
                        + "Internal storage › games › Perlin, so any file manager or PC can open them.\n\n"
                        + "Allow \"All files access\" for Perlin on the next screen, then come back.")
                .setCancelable(false)
                .setPositiveButton("Continue", (d, w) -> request())
                .setNegativeButton("Exit", (d, w) -> finish())
                .show();
    }

    private void request() {
        if (Build.VERSION.SDK_INT >= 30) {
            waitingForSettings = true;
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                try {
                    startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                } catch (Exception again) {
                    openAppSettings();
                }
            }
        } else if (PerlinStorage.prefs(this).getBoolean(PerlinStorage.KEY_ASKED, false)
                && !shouldShowRequestPermissionRationale(Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
            openAppSettings();
        } else {
            PerlinStorage.prefs(this).edit().putBoolean(PerlinStorage.KEY_ASKED, true).apply();
            requestPermissions(new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE,
                    Manifest.permission.READ_EXTERNAL_STORAGE}, REQUEST_LEGACY);
        }
    }

    private void openAppSettings() {
        waitingForSettings = true;
        try {
            startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                    Uri.parse("package:" + getPackageName())));
        } catch (Exception e) {
            waitingForSettings = false;
            Log.w("Perlin", "could not open the app settings", e);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == REQUEST_LEGACY)
            proceed();
    }

    private void proceed() {
        if (started)
            return;
        if (PerlinActivity.alive) {
            launch();
            return;
        }
        if (!PerlinStorage.sharedUsable(this)) {
            ask();
            return;
        }
        if (!PerlinStorage.needsMove(this)) {
            launch();
            return;
        }
        final File from = PerlinStorage.privateDir(this);
        final File to = PerlinStorage.sharedDir();
        if (!PerlinStorage.hasContent(from)) {
            PerlinStorage.prefs(this).edit().putString(PerlinStorage.KEY_DIR, to.getAbsolutePath()).apply();
            launch();
            return;
        }
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(64, 64, 64, 64);
        final TextView label = new TextView(this);
        label.setText("Moving Perlin's files to games/Perlin…");
        label.setGravity(Gravity.CENTER);
        final ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        layout.addView(label);
        layout.addView(bar, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        setContentView(layout);
        new Thread(() -> {
            boolean ok;
            try {
                PerlinStorage.move(from, to, (done, total) -> {
                    final int value = total > 0 ? (int) (done * 1000 / total) : 1000;
                    runOnUiThread(() -> bar.setProgress(value));
                });
                ok = true;
            } catch (Exception e) {
                Log.w("Perlin", "moving data to " + to + " failed", e);
                ok = false;
            }
            final boolean moved = ok;
            runOnUiThread(() -> {
                if (moved) {
                    PerlinStorage.prefs(this).edit().putString(PerlinStorage.KEY_DIR, to.getAbsolutePath()).apply();
                    launch();
                } else {
                    new AlertDialog.Builder(this)
                            .setTitle("Couldn't move Perlin's files")
                            .setMessage("Some files could not be copied to games/Perlin. Nothing was deleted from "
                                    + "the old folder. Free up some space and try again.")
                            .setCancelable(false)
                            .setPositiveButton("Try again", (d, w) -> proceed())
                            .setNegativeButton("Exit", (d, w) -> finish())
                            .show();
                }
            });
        }, "perlin-storage-move").start();
    }

    private void launch() {
        if (started)
            return;
        started = true;
        Intent game = new Intent(this, PerlinActivity.class);
        if (getIntent() != null && getIntent().getData() != null)
            game.setData(getIntent().getData());
        game.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION);
        startActivity(game);
        finish();
        overridePendingTransition(0, 0);
    }
}
