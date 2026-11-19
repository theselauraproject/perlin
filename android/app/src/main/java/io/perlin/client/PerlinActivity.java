package io.perlin.client;

import android.app.Dialog;
import android.app.PendingIntent;
import android.graphics.Color;
import android.net.Uri;
import android.view.Gravity;
import android.webkit.CookieManager;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.widget.FrameLayout;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class PerlinActivity extends SDLActivity {
    static volatile boolean alive = false;

    private boolean redirected = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File data = PerlinStorage.chosenDir(this);
        try {
            Os.setenv("PERLIN_DATA_DIR", data.getAbsolutePath(), true);
        } catch (ErrnoException e) {
            Log.w("Perlin", "could not set the data directory", e);
        }
        if (!PerlinStorage.sharedUsable(this)) {
            redirected = true;
            startActivity(new Intent(this, LauncherActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
            super.onCreate(savedInstanceState);
            finish();
            return;
        }
        alive = true;
        data.mkdirs();
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        installKeyboardResize();
    }

    private void installKeyboardResize() {
        final View decor = getWindow().getDecorView();
        final ViewGroup content = (ViewGroup) findViewById(android.R.id.content);
        decor.getViewTreeObserver().addOnGlobalLayoutListener(new ViewTreeObserver.OnGlobalLayoutListener() {
            @Override
            public void onGlobalLayout() {
                if (content == null || content.getChildCount() == 0)
                    return;
                View child = content.getChildAt(0);
                if (!(child.getLayoutParams() instanceof FrameLayout.LayoutParams))
                    return;
                FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) child.getLayoutParams();
                Rect visible = new Rect();
                decor.getWindowVisibleDisplayFrame(visible);
                int screen = decor.getRootView().getHeight();
                boolean keyboard = screen > 0 && screen - visible.bottom > screen / 5;
                int want = keyboard ? visible.bottom : ViewGroup.LayoutParams.MATCH_PARENT;
                if (lp.height != want) {
                    lp.height = want;
                    child.setLayoutParams(lp);
                }
            }
        });
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    private Dialog signIn;

    public boolean showSignIn(final String url) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    openSignIn(url);
                } catch (Throwable t) {
                    Log.w("Perlin", "in-app sign-in unavailable", t);
                    openBrowser(url);
                }
            }
        });
        return true;
    }

    public boolean closeSignIn() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (signIn != null) {
                    try {
                        signIn.dismiss();
                    } catch (Throwable ignored) {
                    }
                    signIn = null;
                }
            }
        });
        return true;
    }

    private void openBrowser(String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (Throwable t) {
            Log.w("Perlin", "no browser for sign-in", t);
        }
    }

    private void openSignIn(final String url) {
        if (signIn != null) {
            try {
                signIn.dismiss();
            } catch (Throwable ignored) {
            }
        }
        final Dialog dialog = new Dialog(this, android.R.style.Theme_DeviceDefault_NoActionBar_Fullscreen);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(32, 32, 32));
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        int pad = (int) (8 * getResources().getDisplayMetrics().density);
        bar.setPadding(pad, pad / 2, pad, pad / 2);
        TextView title = new TextView(this);
        title.setText("Sign in to Microsoft. Perlin returns here when you finish.");
        title.setTextColor(Color.WHITE);
        bar.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        Button browser = new Button(this);
        browser.setText("Use browser");
        browser.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dialog.dismiss();
                openBrowser(url);
            }
        });
        bar.addView(browser);
        Button close = new Button(this);
        close.setText("Close");
        close.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dialog.dismiss();
            }
        });
        bar.addView(close);
        root.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        final WebView web = new WebView(this);
        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        CookieManager.getInstance().setAcceptCookie(true);
        CookieManager.getInstance().setAcceptThirdPartyCookies(web, true);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                String scheme = request.getUrl().getScheme();
                if ("http".equals(scheme) || "https".equals(scheme))
                    return false;
                try {
                    startActivity(new Intent(Intent.ACTION_VIEW, request.getUrl()));
                } catch (Throwable ignored) {
                }
                return true;
            }
        });
        root.addView(web, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        dialog.setContentView(root);
        dialog.setOnDismissListener(new android.content.DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(android.content.DialogInterface d) {
                web.stopLoading();
                web.destroy();
                if (signIn == dialog)
                    signIn = null;
            }
        });
        signIn = dialog;
        web.loadUrl(url);
        dialog.show();
    }

    private static final String ACTION_INSTALLED = "io.perlin.client.UPDATE_STATUS";

    public boolean installApk(String path) {
        try {
            File apk = new File(path);
            PackageInstaller installer = getPackageManager().getPackageInstaller();
            PackageInstaller.SessionParams params =
                    new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
            params.setAppPackageName(getPackageName());
            int id = installer.createSession(params);
            try (PackageInstaller.Session session = installer.openSession(id)) {
                try (InputStream in = new FileInputStream(apk);
                     OutputStream out = session.openWrite("perlin.apk", 0, apk.length())) {
                    byte[] buffer = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buffer)) > 0)
                        out.write(buffer, 0, n);
                    session.fsync(out);
                }
                Intent status = new Intent(this, PerlinActivity.class).setAction(ACTION_INSTALLED);
                int flags = PendingIntent.FLAG_UPDATE_CURRENT
                        | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
                PendingIntent pending = PendingIntent.getActivity(this, id, status, flags);
                session.commit(pending.getIntentSender());
            }
            return true;
        } catch (Exception e) {
            Log.w("Perlin", "update install failed", e);
            return false;
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (intent == null || !ACTION_INSTALLED.equals(intent.getAction()))
            return;
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) {
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(confirm);
            }
        } else if (status != PackageInstaller.STATUS_SUCCESS) {
            Log.w("Perlin", "update not installed: " + intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE));
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (isFinishing() && !redirected)
            alive = false;
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String[] getArguments() {
        return new String[0];
    }
}
