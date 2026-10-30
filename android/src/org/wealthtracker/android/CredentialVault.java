package org.wealthtracker.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/** Secrets are encrypted with a non-exportable Android Keystore key. */
public final class CredentialVault {
    private static final String ALIAS = "org.wealthtracker.credentials.v1";
    private static final int LIMIT = 65536;
    private CredentialVault() {}

    private static SharedPreferences storage(Context context) {
        return context.getSharedPreferences("credential-vault", Context.MODE_PRIVATE);
    }

    private static SecretKey key(boolean create) throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (store.containsAlias(ALIAS)) return (SecretKey) store.getKey(ALIAS, null);
        if (!create) throw new IllegalStateException("Credential key unavailable");
        KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(ALIAS,
            KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());
        return generator.generateKey();
    }

    public static synchronized void put(Context context, String account, byte[] value) throws Exception {
        if (value == null || value.length == 0 || value.length > LIMIT) throw new IllegalArgumentException();
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, key(true));
        cipher.updateAAD(account.getBytes(StandardCharsets.UTF_8));
        byte[] encrypted = cipher.doFinal(value);
        byte[] iv = cipher.getIV();
        byte[] record = new byte[iv.length + encrypted.length];
        System.arraycopy(iv, 0, record, 0, iv.length);
        System.arraycopy(encrypted, 0, record, iv.length, encrypted.length);
        if (!storage(context).edit().putString(account, Base64.encodeToString(record, Base64.NO_WRAP)).commit())
            throw new IllegalStateException("Credential write failed");
    }

    public static synchronized byte[] get(Context context, String account) throws Exception {
        String encoded = storage(context).getString(account, null);
        if (encoded == null) return null;
        if (encoded.length() > 90000) throw new IllegalArgumentException();
        byte[] record = Base64.decode(encoded, Base64.NO_WRAP);
        if (record.length < 29 || record.length > LIMIT + 28) throw new IllegalArgumentException();
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.DECRYPT_MODE, key(false), new GCMParameterSpec(128, record, 0, 12));
        cipher.updateAAD(account.getBytes(StandardCharsets.UTF_8));
        byte[] result = cipher.doFinal(record, 12, record.length - 12);
        if (result.length == 0 || result.length > LIMIT) {
            Arrays.fill(result, (byte) 0);
            throw new IllegalArgumentException();
        }
        return result;
    }

    public static synchronized void remove(Context context, String account) {
        if (!storage(context).edit().remove(account).commit())
            throw new IllegalStateException("Credential removal failed");
    }
}
