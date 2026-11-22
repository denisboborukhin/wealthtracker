package org.wealthtracker.android;

import android.net.Uri;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import java.util.Collections;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class BankBrowserClientTest {
    @Test public void subresourceFailuresDoNotFailTheBankPage() {
        final int[] errors = {0};
        BankBrowserClient client = new BankBrowserClient(new WebViewClient() {
            @Override public void onReceivedError(WebView view, int code, String description, String url) { errors[0]++; }
        });
        client.onReceivedError(null, request(false), null);
        assertEquals(0, errors[0]);
        WebResourceResponse response = new WebResourceResponse("text/html", "UTF-8", 503, "Synthetic failure", Collections.emptyMap(), null);
        client.onReceivedHttpError(null, request(false), response);
        assertEquals(0, errors[0]);
        client.onReceivedHttpError(null, request(true), response);
        assertEquals(1, errors[0]);
    }

    @Test public void forwardsQtLifecycleAndNavigation() {
        final int[] callbacks = {0};
        BankBrowserClient client = new BankBrowserClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView view, String url) { callbacks[0]++; return true; }
            @Override public void onLoadResource(WebView view, String url) { callbacks[0]++; }
            @Override public void onPageStarted(WebView view, String url, android.graphics.Bitmap icon) { callbacks[0]++; }
            @Override public void onPageFinished(WebView view, String url) { callbacks[0]++; }
            @Override public void onReceivedError(WebView view, int code, String description, String url) { callbacks[0]++; }
        });
        String url = "https://bank-render.invalid/";
        assertTrue(client.shouldOverrideUrlLoading(null, url));
        client.onLoadResource(null, url);
        client.onPageStarted(null, url, null);
        client.onPageFinished(null, url);
        client.onReceivedError(null, WebViewClient.ERROR_CONNECT, "synthetic", url);
        assertTrue(client.shouldOverrideUrlLoading(null, "intent://bank-render.invalid/"));
        assertTrue(client.shouldOverrideUrlLoading(null, "http://bank-render.invalid/"));
        assertEquals(5, callbacks[0]);
    }

    private static WebResourceRequest request(boolean main) {
        return new WebResourceRequest() {
            public Uri getUrl() { return Uri.parse("https://bank-render.invalid/"); }
            public boolean isForMainFrame() { return main; }
            public boolean isRedirect() { return false; }
            public boolean hasGesture() { return false; }
            public String getMethod() { return "GET"; }
            public Map<String,String> getRequestHeaders() { return Collections.emptyMap(); }
        };
    }
}
