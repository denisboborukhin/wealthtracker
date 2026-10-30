package org.wealthtracker.android;

import android.webkit.CookieManager;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;
import static org.junit.Assume.assumeTrue;

@RunWith(AndroidJUnit4.class)
public class BankBrowserStorageTest {
    private void onMain(Runnable task) {
        InstrumentationRegistry.getInstrumentation().runOnMainSync(task);
    }
    private void await(CountDownLatch latch) throws Exception {
        assertTrue("Synthetic WebView operation timed out", latch.await(10, TimeUnit.SECONDS));
    }
    private String read(WebView view) throws Exception {
        AtomicReference<String> result = new AtomicReference<>();
        CountDownLatch done = new CountDownLatch(1);
        onMain(() -> view.evaluateJavascript("localStorage.getItem('probe')", value -> {
            result.set(value); done.countDown();
        }));
        await(done);
        return result.get();
    }
    @Test public void removesOnlyTheSelectedSiteCookiesAndLocalStorage() throws Exception {
        AtomicBoolean supported = new AtomicBoolean();
        onMain(() -> supported.set(BankBrowserStorage.supported()));
        assumeTrue("Provider lacks site-scoped browsing-data deletion", supported.get());
        String[] sites = {"wt-bank-one.example", "wt-bank-two.example"};
        WebView[] views = new WebView[2];
        CountDownLatch loaded = new CountDownLatch(2);
        try {
            onMain(() -> {
                for (int i = 0; i < views.length; i++) {
                    WebView view = new WebView(InstrumentationRegistry.getInstrumentation().getTargetContext());
                    views[i] = view;
                    view.getSettings().setJavaScriptEnabled(true);
                    view.getSettings().setDomStorageEnabled(true);
                    view.setWebViewClient(new WebViewClient() {
                        @Override public void onPageFinished(WebView v, String url) { loaded.countDown(); }
                    });
                    view.loadDataWithBaseURL("https://" + sites[i] + "/",
                        "<html><script>localStorage.setItem('probe','synthetic')</script></html>", "text/html", "UTF-8", null);
                }
            });
            await(loaded);
            assertEquals("\"synthetic\"", read(views[0]));
            assertEquals("\"synthetic\"", read(views[1]));
            CountDownLatch cookies = new CountDownLatch(2);
            onMain(() -> {
                for (String site : sites) CookieManager.getInstance().setCookie("https://" + site + "/",
                    "probe=synthetic; Path=/; Secure", success -> cookies.countDown());
            });
            await(cookies);
            CountDownLatch cleared = new CountDownLatch(1);
            AtomicBoolean success = new AtomicBoolean();
            onMain(() -> BankBrowserStorage.clearSites(new String[]{sites[0]}, ok -> {
                success.set(ok); cleared.countDown();
            }));
            await(cleared);
            assertTrue(success.get());
            assertEquals("null", read(views[0]));
            assertEquals("\"synthetic\"", read(views[1]));
            onMain(() -> {
                assertNull(CookieManager.getInstance().getCookie("https://" + sites[0] + "/"));
                assertTrue(CookieManager.getInstance().getCookie("https://" + sites[1] + "/").contains("probe=synthetic"));
            });
        } finally {
            onMain(() -> { for (WebView view : views) if (view != null) view.destroy(); });
            CountDownLatch cleanup = new CountDownLatch(1);
            onMain(() -> BankBrowserStorage.clearSites(sites, ok -> cleanup.countDown()));
            await(cleanup);
        }
    }
}
