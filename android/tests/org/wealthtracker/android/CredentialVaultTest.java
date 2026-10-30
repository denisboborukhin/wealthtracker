package org.wealthtracker.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Base64;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.Arrays;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class CredentialVaultTest {
    private final Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    private final byte[] sample = "synthetic Android credential".getBytes(StandardCharsets.UTF_8);

    @Test public void roundTripAndRemoval() throws Exception {
        String name = "android-test/roundtrip";
        CredentialVault.put(context, name, sample);
        assertArrayEquals(sample, CredentialVault.get(context, name));
        String record = context.getSharedPreferences("credential-vault", 0).getString(name, "");
        assertFalse(record.contains(new String(sample, StandardCharsets.UTF_8)));
        KeyStore keys = KeyStore.getInstance("AndroidKeyStore");
        keys.load(null);
        assertNull(keys.getKey("org.wealthtracker.credentials.v1", null).getEncoded());
        CredentialVault.remove(context, name);
        assertNull(CredentialVault.get(context, name));
    }

    @Test public void tamperingAndAccountTransplantAreRejected() throws Exception {
        String name = "android-test/tampering";
        String other = "android-test/transplant";
        SharedPreferences prefs = context.getSharedPreferences("credential-vault", 0);
        try {
            CredentialVault.put(context, name, sample);
            String encoded = prefs.getString(name, null);
            prefs.edit().putString(other, encoded).commit();
            try { CredentialVault.get(context, other); fail("Transplant accepted"); }
            catch (java.security.GeneralSecurityException expected) { }
            byte[] damaged = Base64.decode(encoded, Base64.NO_WRAP);
            damaged[damaged.length - 1] ^= 1;
            prefs.edit().putString(name, Base64.encodeToString(damaged, Base64.NO_WRAP)).commit();
            try { CredentialVault.get(context, name); fail("Tampering accepted"); }
            catch (java.security.GeneralSecurityException expected) { }
        } finally {
            CredentialVault.remove(context, name);
            CredentialVault.remove(context, other);
        }
    }

    @Test public void rejectsOversizedAndEmptyValues() throws Exception {
        for (int size : new int[]{0, 65537}) {
            try { CredentialVault.put(context, "android-test/invalid", new byte[size]); fail("Invalid size accepted"); }
            catch (IllegalArgumentException expected) { }
        }
        assertNull(CredentialVault.get(context, "android-test/invalid"));
    }

    @Test public void persistenceAcrossProcessRestart() throws Exception {
        String name = "android-test/persistence";
        if ("read".equals(InstrumentationRegistry.getArguments().getString("phase"))) {
            assertArrayEquals(sample, CredentialVault.get(context, name));
            CredentialVault.remove(context, name);
        } else {
            CredentialVault.put(context, name, sample);
        }
    }
}
