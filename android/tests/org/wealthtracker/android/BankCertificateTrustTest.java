package org.wealthtracker.android;

import android.net.http.X509TrustManagerExtensions;
import android.security.NetworkSecurityPolicy;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.Collection;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class BankCertificateTrustTest {
    private X509TrustManagerExtensions trust() throws Exception {
        TrustManagerFactory factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        factory.init((KeyStore) null);
        for (TrustManager manager : factory.getTrustManagers())
            if (manager instanceof X509TrustManager)
                return new X509TrustManagerExtensions((X509TrustManager) manager);
        throw new AssertionError("No Android X509 trust manager");
    }

    private X509Certificate[] chain(String bank) throws Exception {
        try (InputStream stream = InstrumentationRegistry.getInstrumentation().getContext()
                .getAssets().open(bank + "_public_chain.pem")) {
            Collection<? extends java.security.cert.Certificate> certificates =
                CertificateFactory.getInstance("X.509").generateCertificates(stream);
            return certificates.toArray(new X509Certificate[0]);
        }
    }

    private void rejected(X509Certificate[] chain, String host) throws Exception {
        try {
            trust().checkServerTrusted(chain, "RSA", host);
            fail("Untrusted certificate chain was accepted for " + host);
        } catch (CertificateException expected) { }
    }

    @Test public void validatesAllThreePublicBankChains() throws Exception {
        String[] banks = {"alfa", "vtb", "tbank"};
        String[] hosts = {"web.alfabank.ru", "online.vtb.ru", "www.tbank.ru"};
        for (int i = 0; i < banks.length; i++) {
            X509Certificate[] chain = chain(banks[i]);
            for (X509Certificate certificate : chain) certificate.checkValidity();
            assertFalse(trust().checkServerTrusted(chain, "RSA", hosts[i]).isEmpty());
            assertFalse(NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted(hosts[i]));
        }
    }

    @Test public void additionalRootIsRestrictedToExactBankHosts() throws Exception {
        X509Certificate[] chain = chain("tbank");
        for (String host : new String[]{"example.com", "unlisted.tbank.ru", "www.tbank.ru.example.com"})
            rejected(chain, host);
    }

    @Test public void validatesPublicBankAssetChains() throws Exception {
        String[] fixtures = {"vtb_s1", "vtb_s2", "tbank_cdn"};
        String[] hosts = {"s1.vtb.ru", "s2.vtb.ru", "cdn.tbank.ru"};
        for (int i = 0; i < fixtures.length; i++) {
            X509Certificate[] chain = chain(fixtures[i]);
            for (X509Certificate certificate : chain) certificate.checkValidity();
            assertFalse(trust().checkServerTrusted(chain, "RSA", hosts[i]).isEmpty());
            assertFalse(NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted(hosts[i]));
        }
    }

    @Test public void assetTrustDoesNotIncludeUnlistedSubdomains() throws Exception {
        rejected(chain("vtb_s1"), "unlisted.vtb.ru");
        rejected(chain("vtb_s1"), "sub.s1.vtb.ru");
        rejected(chain("vtb_s2"), "sub.s2.vtb.ru");
        rejected(chain("vtb_s2"), "s2.vtb.ru.example.com");
        rejected(chain("tbank_cdn"), "sub.cdn.tbank.ru");
        rejected(chain("tbank_cdn"), "cdn.tbank.ru.example.com");
    }

    @Test public void rejectsInvalidBankCertificateSignature() throws Exception {
        X509Certificate[] chain = chain("vtb");
        byte[] encoded = chain[0].getEncoded();
        encoded[encoded.length - 1] ^= 1;
        chain[0] = (X509Certificate) CertificateFactory.getInstance("X.509")
            .generateCertificate(new ByteArrayInputStream(encoded));
        rejected(chain, "online.vtb.ru");
    }

    @Test public void bundledRootMatchesVerifiedOfficialDownload() throws Exception {
        var context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        int resource = context.getResources().getIdentifier("russian_trusted_root_ca", "raw", context.getPackageName());
        assertNotEquals(0, resource);
        try (InputStream stream = context.getResources().openRawResource(resource)) {
            X509Certificate root = (X509Certificate) CertificateFactory.getInstance("X.509").generateCertificate(stream);
            root.checkValidity();
            root.verify(root.getPublicKey());
            assertTrue(root.getBasicConstraints() >= 0);
            StringBuilder digest = new StringBuilder();
            for (byte value : MessageDigest.getInstance("SHA-256").digest(root.getEncoded()))
                digest.append(String.format("%02x", value & 255));
            assertEquals("d26d2d0231b7c39f92cc738512ba54103519e4405d68b5bd703e9788ca8ecf31", digest.toString());
        }
    }
}
