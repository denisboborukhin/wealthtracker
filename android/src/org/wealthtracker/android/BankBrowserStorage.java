package org.wealthtracker.android;

import android.webkit.WebStorage;
import androidx.webkit.WebStorageCompat;
import androidx.webkit.WebViewFeature;
import java.util.function.Consumer;

/** Clear cookies and DOM storage for one bank without touching other banks. */
public final class BankBrowserStorage {
    private BankBrowserStorage() {}
    private static native void completed(long token, boolean success);

    public static boolean supported() {
        return WebViewFeature.isFeatureSupported(WebViewFeature.DELETE_BROWSING_DATA);
    }

    public static void clear(String[] domains, long token) {
        clearSites(domains, success -> completed(token, success));
    }

    static void clearSites(String[] domains, Consumer<Boolean> done) {
        if (!supported()) { done.accept(false); return; }
        clearNext(domains, 0, done);
    }

    private static void clearNext(String[] domains, int index, Consumer<Boolean> done) {
        if (index == domains.length) { done.accept(true); return; }
        try {
            WebStorageCompat.deleteBrowsingDataForSite(WebStorage.getInstance(), domains[index],
                () -> clearNext(domains, index + 1, done));
        } catch (RuntimeException ignored) { done.accept(false); }
    }
}
