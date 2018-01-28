// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import java.lang.reflect.Method;
import java.security.NoSuchAlgorithmException;
import java.security.PrivateKey;
import java.security.Signature;
import java.security.interfaces.RSAPrivateKey;

/**
 * Specifies all the dependencies from the native OpenSSL engine on an Android KeyStore.
 */
@JNINamespace("net::android")
public class AndroidKeyStore {
    private static final String TAG = "AndroidKeyStore";

    /**
     * Sign a given message with a given PrivateKey object. This method
     * shall only be used to implement signing in the context of SSL
     * client certificate support.
     *
     * The message will actually be a hash, computed by OpenSSL itself,
     * depending on the type of the key. The result should match exactly
     * what the vanilla implementations of the following OpenSSL function
     * calls do:
     *
     *  - For a RSA private key, this should be equivalent to calling
     *    RSA_private_encrypt(..., RSA_PKCS1_PADDING), i.e. it must
     *    generate a raw RSA signature. The message must be either a
     *    combined, 36-byte MD5+SHA1 message digest or a DigestInfo
     *    value wrapping a message digest.
     *
     *  - For a ECDSA private keys, this should be equivalent to calling
     *    ECDSA_sign(0,...). The message must be a hash and the function shall
     *    compute a direct ECDSA signature for it.
     *
     * @param privateKey The PrivateKey handle.
     * @param message The message to sign.
     * @return signature as a byte buffer.
     *
     * Important: Due to a platform bug, this function will always fail on
     *            Android < 4.2 for RSA PrivateKey objects. See the
     *            getOpenSSLHandleForPrivateKey() below for work-around.
     */
    @CalledByNative
    private static byte[] rawSignDigestWithPrivateKey(PrivateKey privateKey, byte[] message) {
        // Get the Signature for this key.
        Signature signature = null;
        // Hint: Algorithm names come from:
        // http://docs.oracle.com/javase/6/docs/technotes/guides/security/StandardNames.html
        try {
            String keyAlgorithm = privateKey.getAlgorithm();
            if ("RSA".equalsIgnoreCase(keyAlgorithm)) {
                // IMPORTANT: Due to a platform bug, this will throw NoSuchAlgorithmException
                // on Android 4.1.x. Fixed in 4.2 and higher.
                // See https://android-review.googlesource.com/#/c/40352/
                signature = Signature.getInstance("NONEwithRSA");
            } else if ("EC".equalsIgnoreCase(keyAlgorithm)) {
                signature = Signature.getInstance("NONEwithECDSA");
            }
        } catch (NoSuchAlgorithmException e) {
            // Intentionally do nothing.
        }

        if (signature == null) {
            Log.e(TAG, "Unsupported private key algorithm: " + privateKey.getAlgorithm());
            return null;
        }

        // Sign the message.
        try {
            signature.initSign(privateKey);
            signature.update(message);
            return signature.sign();
        } catch (Exception e) {
            Log.e(TAG, "Exception while signing message with " + privateKey.getAlgorithm()
                    + " private key: " + e);
            return null;
        }
    }

    private static Object getOpenSSLKeyForPrivateKey(PrivateKey privateKey) {
        // Sanity checks
        if (privateKey == null) {
            Log.e(TAG, "privateKey == null");
            return null;
        }
        if (!(privateKey instanceof RSAPrivateKey)) {
            Log.e(TAG, "does not implement RSAPrivateKey");
            return null;
        }
        // First, check that this is a proper instance of OpenSSLRSAPrivateKey
        // or one of its sub-classes.
        Class<?> superClass;
        try {
            superClass =
                    Class.forName("org.apache.harmony.xnet.provider.jsse.OpenSSLRSAPrivateKey");
        } catch (Exception e) {
            // This may happen if the target device has a completely different
            // implementation of the java.security APIs, compared to vanilla
            // Android. Highly unlikely, but still possible.
            Log.e(TAG, "Cannot find system OpenSSLRSAPrivateKey class: " + e);
            return null;
        }
        if (!superClass.isInstance(privateKey)) {
            // This may happen if the PrivateKey was not created by the "AndroidOpenSSL"
            // provider, which should be the default. That could happen if an OEM decided
            // to implement a different default provider. Also highly unlikely.
            Log.e(TAG, "Private key is not an OpenSSLRSAPrivateKey instance, its class name is:"
                    + privateKey.getClass().getCanonicalName());
            return null;
        }

        try {
            // Use reflection to invoke the 'getOpenSSLKey()' method on the
            // private key. This returns another Java object that wraps a native
            // EVP_PKEY and OpenSSLEngine. Note that the method is final in Android
            // 4.1, so calling the superclass implementation is ok.
            Method getKey = superClass.getDeclaredMethod("getOpenSSLKey");
            getKey.setAccessible(true);
            Object opensslKey = null;
            try {
                opensslKey = getKey.invoke(privateKey);
            } finally {
                getKey.setAccessible(false);
            }
            if (opensslKey == null) {
                // Bail when detecting OEM "enhancement".
                Log.e(TAG, "getOpenSSLKey() returned null");
                return null;
            }
            return opensslKey;
        } catch (Exception e) {
            Log.e(TAG, "Exception while trying to retrieve system EVP_PKEY handle: " + e);
            return null;
        }
    }

    /**
     * Return the system EVP_PKEY handle corresponding to a given PrivateKey
     * object.
     *
     * This shall only be used when the "NONEwithRSA" signature is not
     * available, as described in rawSignDigestWithPrivateKey(). I.e.
     * never use this on Android 4.2 or higher.
     *
     * This can only work in Android 4.0.4 and higher, for older versions
     * of the platform (e.g. 4.0.3), there is no system OpenSSL EVP_PKEY,
     * but the private key contents can be retrieved directly with
     * the getEncoded() method.
     *
     * This assumes that the target device uses a vanilla AOSP
     * implementation of its java.security classes, which is also
     * based on OpenSSL (fortunately, no OEM has apperently changed to
     * a different implementation, according to the Android team).
     *
     * Note that the object returned was created with the platform version of
     * OpenSSL, and _not_ the one that comes with Chromium. It may not be used
     * with the Chromium version of OpenSSL (BoringSSL). See AndroidEVP_PKEY in
     * net/android/legacy_openssl.h.
     *
     * To better understand what's going on below, please refer to the
     * following source files in the Android 4.0.4 and 4.1 source trees:
     * libcore/luni/src/main/java/org/apache/harmony/xnet/provider/jsse/OpenSSLRSAPrivateKey.java
     * libcore/luni/src/main/native/org_apache_harmony_xnet_provider_jsse_NativeCrypto.cpp
     *
     * @param privateKey The PrivateKey handle.
     * @return The EVP_PKEY handle, as a 32-bit integer (0 if not available)
     */
    @CalledByNative
    private static long getOpenSSLHandleForPrivateKey(PrivateKey privateKey) {
        Object opensslKey = getOpenSSLKeyForPrivateKey(privateKey);
        if (opensslKey == null) return 0;

        try {
            // Use reflection to invoke the 'getPkeyContext' method on the
            // result of the getOpenSSLKey(). This is an 32-bit integer
            // which is the address of an EVP_PKEY object. Note that this
            // method these days returns a 64-bit long, but since this code
            // path is used for older Android versions, it may still return
            // a 32-bit int here. To be on the safe side, we cast the return
            // value via Number rather than directly to Integer or Long.
            Method getPkeyContext;
            try {
                getPkeyContext = opensslKey.getClass().getDeclaredMethod("getPkeyContext");
            } catch (Exception e) {
                // Bail here too, something really not working as expected.
                Log.e(TAG, "No getPkeyContext() method on OpenSSLKey member:" + e);
                return 0;
            }
            getPkeyContext.setAccessible(true);
            long evp_pkey = 0;
            try {
                evp_pkey = ((Number) getPkeyContext.invoke(opensslKey)).longValue();
            } finally {
                getPkeyContext.setAccessible(false);
            }
            if (evp_pkey == 0) {
                // The PrivateKey is probably rotten for some reason.
                Log.e(TAG, "getPkeyContext() returned null");
            }
            return evp_pkey;

        } catch (Exception e) {
            Log.e(TAG, "Exception while trying to retrieve system EVP_PKEY handle: " + e);
            return 0;
        }
    }

    /**
     * Return the OpenSSLEngine object corresponding to a given PrivateKey
     * object.
     *
     * This shall only be used for Android 4.1 to work around a platform bug.
     * See https://crbug.com/381465.
     *
     * @param privateKey The PrivateKey handle.
     * @return The OpenSSLEngine object (or null if not available)
     */
    @CalledByNative
    private static Object getOpenSSLEngineForPrivateKey(PrivateKey privateKey) {
        // Find the system OpenSSLEngine class.
        Class<?> engineClass;
        try {
            engineClass = Class.forName("org.apache.harmony.xnet.provider.jsse.OpenSSLEngine");
        } catch (Exception e) {
            // This may happen if the target device has a completely different
            // implementation of the java.security APIs, compared to vanilla
            // Android. Highly unlikely, but still possible.
            Log.e(TAG, "Cannot find system OpenSSLEngine class: " + e);
            return null;
        }

        Object opensslKey = getOpenSSLKeyForPrivateKey(privateKey);
        if (opensslKey == null) return null;

        try {
            // Use reflection to invoke the 'getEngine' method on the
            // result of the getOpenSSLKey().
            Method getEngine;
            try {
                getEngine = opensslKey.getClass().getDeclaredMethod("getEngine");
            } catch (Exception e) {
                // Bail here too, something really not working as expected.
                Log.e(TAG, "No getEngine() method on OpenSSLKey member:" + e);
                return null;
            }
            getEngine.setAccessible(true);
            Object engine = null;
            try {
                engine = getEngine.invoke(opensslKey);
            } finally {
                getEngine.setAccessible(false);
            }
            if (engine == null) {
                // The PrivateKey is probably rotten for some reason.
                Log.e(TAG, "getEngine() returned null");
            }
            // Sanity-check the returned engine.
            if (!engineClass.isInstance(engine)) {
                Log.e(TAG, "Engine is not an OpenSSLEngine instance, its class name is:"
                        + engine.getClass().getCanonicalName());
                return null;
            }
            return engine;

        } catch (Exception e) {
            Log.e(TAG, "Exception while trying to retrieve OpenSSLEngine object: " + e);
            return null;
        }
    }
}
