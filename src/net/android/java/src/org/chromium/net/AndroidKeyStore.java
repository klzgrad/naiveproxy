// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import java.security.NoSuchAlgorithmException;
import java.security.PrivateKey;
import java.security.Signature;

/**
 * Specifies all the dependencies from the native OpenSSL engine on an Android KeyStore.
 */
@JNINamespace("net::android")
public class AndroidKeyStore {
    private static final String TAG = "AndroidKeyStore";

    @CalledByNative
    private static String getPrivateKeyClassName(PrivateKey privateKey) {
        return privateKey.getClass().getName();
    }

    /**
     * Sign a given message with a given PrivateKey object.
     *
     * @param privateKey The PrivateKey handle.
     * @param algorithm The signature algorithm to use.
     * @param message The message to sign.
     * @return signature as a byte buffer.
     *
     * Note: NONEwithRSA is not implemented in Android < 4.2. See
     * getOpenSSLHandleForPrivateKey() below for a work-around.
     */
    @CalledByNative
    private static byte[] signWithPrivateKey(
            PrivateKey privateKey, String algorithm, byte[] message) {
        // Hint: Algorithm names come from:
        // http://docs.oracle.com/javase/6/docs/technotes/guides/security/StandardNames.html
        Signature signature = null;
        try {
            signature = Signature.getInstance(algorithm);
        } catch (NoSuchAlgorithmException e) {
            Log.e(TAG, "Signature algorithm " + algorithm + " not supported: " + e);
            return null;
        }

        try {
            signature.initSign(privateKey);
            signature.update(message);
            return signature.sign();
        } catch (Exception e) {
            Log.e(TAG,
                    "Exception while signing message with " + algorithm + " and "
                            + privateKey.getAlgorithm() + " private key ("
                            + privateKey.getClass().getName() + "): " + e);
            return null;
        }
    }
}
