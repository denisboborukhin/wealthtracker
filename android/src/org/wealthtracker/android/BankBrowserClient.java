package org.wealthtracker.android;

import android.graphics.Bitmap;
import android.net.http.SslError;
import android.net.Uri;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.SslErrorHandler;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;

/** Preserve Qt callbacks while reporting failures that Qt 6.11 does not forward. */
public final class BankBrowserClient extends WebViewClient {
    private final WebViewClient delegate;
    BankBrowserClient(WebViewClient delegate) { this.delegate = delegate; }

    public static void attach(View root) { attach(root, 20); }
    private static void attach(View root, int attempts) {
        if (install(root) || attempts <= 0) return;
        root.postDelayed(() -> attach(root, attempts - 1), 50);
    }
    private static boolean install(View view) {
        if (view instanceof WebView) {
            WebView web = (WebView) view;
            WebViewClient client = web.getWebViewClient();
            if (client instanceof BankBrowserClient) return true;
            if (client == null || !client.getClass().getName().startsWith(
                    "org.qtproject.qt.android.view.QtAndroidWebViewController$")) return false;
            web.setWebViewClient(new BankBrowserClient(client));
            return true;
        }
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i)
                if (install(group.getChildAt(i))) return true;
        }
        return false;
    }

    @Override public boolean shouldOverrideUrlLoading(WebView view, String url) {
        Uri target = Uri.parse(url);
        if (!"https".equalsIgnoreCase(target.getScheme()) || target.getHost() == null
                || target.getUserInfo() != null || (target.getPort() != -1 && target.getPort() != 443)) return true;
        return delegate.shouldOverrideUrlLoading(view, url);
    }
    @Override public void onLoadResource(WebView view, String url) { delegate.onLoadResource(view, url); }
    @Override public void onPageStarted(WebView view, String url, Bitmap icon) { delegate.onPageStarted(view, url, icon); }
    @Override public void onPageFinished(WebView view, String url) { delegate.onPageFinished(view, url); }
    @Override public void onReceivedError(WebView view, int code, String description, String url) {
        delegate.onReceivedError(view, code, description, url);
    }
    @Override public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
        if (request.isForMainFrame()) delegate.onReceivedError(view, error.getErrorCode(),
            "Bank page network request failed", request.getUrl().toString());
    }
    @Override public void onReceivedHttpError(WebView view, WebResourceRequest request, WebResourceResponse response) {
        if (request.isForMainFrame()) delegate.onReceivedError(view, ERROR_UNKNOWN,
            "Bank page returned an HTTP error", request.getUrl().toString());
    }
    @Override public void onReceivedSslError(WebView view, SslErrorHandler handler, SslError error) {
        handler.cancel();
        String mainPage = view.getUrl();
        if (mainPage == null || mainPage.equals(error.getUrl()))
            delegate.onReceivedError(view, ERROR_FAILED_SSL_HANDSHAKE,
                "Bank page certificate verification failed", error.getUrl());
    }
}
