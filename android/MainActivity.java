package ru.kengaos.shell;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebView;
import android.webkit.WebSettings;

/* KengaOS Mobile — WebView-оболочка (этап «оболочка поверх Android»).
   Ассеты — собранный dist (mobile.html). Никаких данных не трогает. */
public class MainActivity extends Activity {
    private WebView wv;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        wv = new WebView(this);
        WebSettings s = wv.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        setContentView(wv);
        wv.loadUrl("file:///android_asset/mobile.html");
    }

    @Override public void onBackPressed() {
        if (wv != null && wv.canGoBack()) wv.goBack(); else super.onBackPressed();
    }
}
