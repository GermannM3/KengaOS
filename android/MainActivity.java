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
import android.webkit.PermissionRequest;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import android.webkit.WebSettings;

/* KengaOS Mobile — WebView-оболочка (этап «оболочка поверх Android»).
   Ассеты — собранный dist (mobile.html). Никаких данных не меняет.
   Мост KengaNative: контакты (чтение), SMS (чтение), вызов и ответ
   через системные приложения, камера через getUserMedia. */
public class MainActivity extends Activity {
    private static final int REQ_CONTACTS = 1;
    private WebView wv;

    private boolean granted(String perm) {
        return checkSelfPermission(perm) == PackageManager.PERMISSION_GRANTED;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (!granted(Manifest.permission.READ_CONTACTS)) {
            requestPermissions(new String[]{Manifest.permission.READ_CONTACTS}, REQ_CONTACTS);
        }
        wv = new WebView(this);
        WebSettings s = wv.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        wv.addJavascriptInterface(new Bridge(), "KengaNative");
        /* getUserMedia в WebView: разрешаем запросы камеры/микрофона */
        wv.setWebChromeClient(new WebChromeClient() {
            @Override public void onPermissionRequest(final PermissionRequest request) {
                runOnUiThread(new Runnable() {
                    @Override public void run() {
                        request.grant(request.getResources());
                    }
                });
            }
        });
        setContentView(wv);
        wv.loadUrl("file:///android_asset/mobile.html");
    }

    @Override public void onBackPressed() {
        if (wv != null && wv.canGoBack()) wv.goBack(); else super.onBackPressed();
    }

    private static String jstr(String v) {
        return v == null ? "" : v.replace("\\", "\\\\").replace("\"", "'");
    }

    private class Bridge {
        /* контакты: [{"n":"Имя","t":"+7..."}, ...] — только имя и номер */
        @JavascriptInterface public String contacts() {
            if (!granted(Manifest.permission.READ_CONTACTS)) return "[]";
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
                        if (sb.length() > 1) sb.append(",");
                        sb.append("{\"n\":\"").append(jstr(name))
                          .append("\",\"t\":\"").append(jstr(tel)).append("\"}");
                    }
                }
            } catch (SecurityException e) {
                return "[]";
            } finally {
                if (c != null) c.close();
            }
            return sb.append("]").toString();
        }

        /* входящие SMS: [{"a":"адрес","b":"текст","d":мс}, ...] — только чтение */
        @JavascriptInterface public String sms() {
            if (!granted(Manifest.permission.READ_SMS)) return "[]";
            StringBuilder sb = new StringBuilder("[");
            Cursor c = null;
            try {
                c = getContentResolver().query(
                    Uri.parse("content://sms/inbox"),
                    new String[]{"address", "body", "date"},
                    null, null, "date DESC");
                if (c != null) {
                    int a = c.getColumnIndex("address");
                    int b = c.getColumnIndex("body");
                    int d = c.getColumnIndex("date");
                    while (c.moveToNext() && sb.length() < 90000) {
                        String addr = c.getString(a), body = c.getString(b);
                        if (addr == null || body == null) continue;
                        if (sb.length() > 1) sb.append(",");
                        sb.append("{\"a\":\"").append(jstr(addr))
                          .append("\",\"b\":\"").append(jstr(body.replace("\n", " ")))
                          .append("\",\"d\":").append(c.getString(d)).append("}");
                    }
                }
            } catch (SecurityException e) {
                return "[]";
            } finally {
                if (c != null) c.close();
            }
            return sb.append("]").toString();
        }

        /* ответ на SMS — через системное приложение (запись SMS недоступна не-дефолтным) */
        @JavascriptInterface public void smsOpen(final String addr) {
            Intent i = new Intent(Intent.ACTION_SENDTO, Uri.parse("smsto:" + addr));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
        }

        /* реальный вызов через системный дозваниватель (громкая связь там же) */
        @JavascriptInterface public void dial(final String num) {
            Intent i = new Intent(Intent.ACTION_DIAL, Uri.parse("tel:" + num));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
        }
    }
}
