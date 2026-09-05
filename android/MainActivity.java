package ru.kengaos.shell;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.ContactsContract;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;
import android.webkit.WebSettings;

/* KengaOS Mobile — WebView-оболочка (этап «оболочка поверх Android»).
   Ассеты — собранный dist (mobile.html). Никаких данных не меняет.
   Мост KengaNative: контакты (только чтение) + реальный вызов (ACTION_DIAL). */
public class MainActivity extends Activity {
    private static final int REQ_CONTACTS = 1;
    private WebView wv;

    private boolean hasContacts() {
        return checkSelfPermission(Manifest.permission.READ_CONTACTS)
            == PackageManager.PERMISSION_GRANTED;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (!hasContacts()) {
            requestPermissions(new String[]{Manifest.permission.READ_CONTACTS}, REQ_CONTACTS);
        }
        wv = new WebView(this);
        WebSettings s = wv.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        wv.addJavascriptInterface(new Bridge(), "KengaNative");
        setContentView(wv);
        wv.loadUrl("file:///android_asset/mobile.html");
    }

    @Override public void onBackPressed() {
        if (wv != null && wv.canGoBack()) wv.goBack(); else super.onBackPressed();
    }

    private class Bridge {
        /* JSON: [{"n":"Имя","t":"+7..."}, ...] — только имя и номер */
        @JavascriptInterface public String contacts() {
            if (!hasContacts()) return "[]";
            StringBuilder sb = new StringBuilder("[");
            Cursor c = null;
            try {
                c = getContentResolver().query(
                    ContactsContract.CommonDataKinds.Phone.CONTENT_URI,
                    new String[]{
                        ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME,
                        ContactsContract.CommonDataKinds.Phone.NUMBER},
                    null, null,
                    ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME + " ASC");
                if (c != null) {
                    int n = c.getColumnIndex(ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME);
                    int t = c.getColumnIndex(ContactsContract.CommonDataKinds.Phone.NUMBER);
                    while (c.moveToNext() && sb.length() < 60000) {
                        String name = c.getString(n), tel = c.getString(t);
                        if (name == null || tel == null) continue;
                        name = name.replace("\"", "'");
                        tel = tel.replace("\"", "");
                        if (sb.length() > 1) sb.append(",");
                        sb.append("{\"n\":\"").append(name)
                          .append("\",\"t\":\"").append(tel).append("\"}");
                    }
                }
            } catch (SecurityException e) {
                return "[]";
            } finally {
                if (c != null) c.close();
            }
            return sb.append("]").toString();
        }

        /* реальный вызов через системный дозваниватель (громкая связь там же) */
        @JavascriptInterface public void dial(final String num) {
            Intent i = new Intent(Intent.ACTION_DIAL, Uri.parse("tel:" + num));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
        }
    }
}
