// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.annotation.TargetApi;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.TrafficStats;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.security.KeyChain;
import android.security.NetworkSecurityPolicy;
import android.telephony.TelephonyManager;
import android.util.Log;

import org.chromium.base.ContextUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.CalledByNativeUnchecked;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.net.SocketImpl;
import java.net.URLConnection;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateException;
import java.util.Enumeration;
import java.util.List;

/**
 * This class implements net utilities required by the net component.
 */
class AndroidNetworkLibrary {

    private static final String TAG = "AndroidNetworkLibrary";

    /**
     * Stores the key pair through the CertInstaller activity.
     * @param publicKey The public key bytes as DER-encoded SubjectPublicKeyInfo (X.509)
     * @param privateKey The private key as DER-encoded PrivateKeyInfo (PKCS#8).
     * @return: true on success, false on failure.
     *
     * Note that failure means that the function could not launch the CertInstaller
     * activity. Whether the keys are valid or properly installed will be indicated
     * by the CertInstaller UI itself.
     */
    @CalledByNative
    public static boolean storeKeyPair(byte[] publicKey, byte[] privateKey) {
        // TODO(digit): Use KeyChain official extra values to pass the public and private
        // keys when they're available. The "KEY" and "PKEY" hard-coded constants were taken
        // from the platform sources, since there are no official KeyChain.EXTRA_XXX definitions
        // for them. b/5859651
        try {
            Intent intent = KeyChain.createInstallIntent();
            intent.putExtra("PKEY", privateKey);
            intent.putExtra("KEY", publicKey);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ContextUtils.getApplicationContext().startActivity(intent);
            return true;
        } catch (ActivityNotFoundException e) {
            Log.w(TAG, "could not store key pair: " + e);
        }
        return false;
    }

    /**
     * @return the mime type (if any) that is associated with the file
     *         extension. Returns null if no corresponding mime type exists.
     */
    @CalledByNative
    public static String getMimeTypeFromExtension(String extension) {
        return URLConnection.guessContentTypeFromName("foo." + extension);
    }

    /**
     * @return true if it can determine that only loopback addresses are
     *         configured. i.e. if only 127.0.0.1 and ::1 are routable. Also
     *         returns false if it cannot determine this.
     */
    @CalledByNative
    public static boolean haveOnlyLoopbackAddresses() {
        Enumeration<NetworkInterface> list = null;
        try {
            list = NetworkInterface.getNetworkInterfaces();
            if (list == null) return false;
        } catch (Exception e) {
            Log.w(TAG, "could not get network interfaces: " + e);
            return false;
        }

        while (list.hasMoreElements()) {
            NetworkInterface netIf = list.nextElement();
            try {
                if (netIf.isUp() && !netIf.isLoopback()) return false;
            } catch (SocketException e) {
                continue;
            }
        }
        return true;
    }

    /**
     * Validate the server's certificate chain is trusted. Note that the caller
     * must still verify the name matches that of the leaf certificate.
     *
     * @param certChain The ASN.1 DER encoded bytes for certificates.
     * @param authType The key exchange algorithm name (e.g. RSA).
     * @param host The hostname of the server.
     * @return Android certificate verification result code.
     */
    @CalledByNative
    public static AndroidCertVerifyResult verifyServerCertificates(byte[][] certChain,
                                                                   String authType,
                                                                   String host) {
        try {
            return X509Util.verifyServerCertificates(certChain, authType, host);
        } catch (KeyStoreException e) {
            return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
        } catch (NoSuchAlgorithmException e) {
            return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
        } catch (IllegalArgumentException e) {
            return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
        }
    }

    /**
     * Adds a test root certificate to the local trust store.
     * @param rootCert DER encoded bytes of the certificate.
     */
    @CalledByNativeUnchecked
    public static void addTestRootCertificate(byte[] rootCert) throws CertificateException,
            KeyStoreException, NoSuchAlgorithmException {
        X509Util.addTestRootCertificate(rootCert);
    }

    /**
     * Removes all test root certificates added by |addTestRootCertificate| calls from the local
     * trust store.
     */
    @CalledByNativeUnchecked
    public static void clearTestRootCertificates() throws NoSuchAlgorithmException,
            CertificateException, KeyStoreException {
        X509Util.clearTestRootCertificates();
    }

    /**
     * Returns the ISO country code equivalent of the current MCC.
     */
    @CalledByNative
    private static String getNetworkCountryIso() {
        TelephonyManager telephonyManager =
                (TelephonyManager) ContextUtils.getApplicationContext().getSystemService(
                        Context.TELEPHONY_SERVICE);
        if (telephonyManager == null) return "";
        return telephonyManager.getNetworkCountryIso();
    }

    /**
     * Returns the MCC+MNC (mobile country code + mobile network code) as
     * the numeric name of the current registered operator.
     */
    @CalledByNative
    private static String getNetworkOperator() {
        TelephonyManager telephonyManager =
                (TelephonyManager) ContextUtils.getApplicationContext().getSystemService(
                        Context.TELEPHONY_SERVICE);
        if (telephonyManager == null) return "";
        return telephonyManager.getNetworkOperator();
    }

    /**
     * Returns the MCC+MNC (mobile country code + mobile network code) as
     * the numeric name of the current SIM operator.
     */
    @CalledByNative
    private static String getSimOperator() {
        TelephonyManager telephonyManager =
                (TelephonyManager) ContextUtils.getApplicationContext().getSystemService(
                        Context.TELEPHONY_SERVICE);
        if (telephonyManager == null) return "";
        return telephonyManager.getSimOperator();
    }

    /**
     * Indicates whether the device is roaming on the currently active network. When true, it
     * suggests that use of data may incur extra costs.
     */
    @CalledByNative
    private static boolean getIsRoaming() {
        ConnectivityManager connectivityManager =
                (ConnectivityManager) ContextUtils.getApplicationContext().getSystemService(
                        Context.CONNECTIVITY_SERVICE);
        NetworkInfo networkInfo = connectivityManager.getActiveNetworkInfo();
        if (networkInfo == null) return false; // No active network.
        return networkInfo.isRoaming();
    }

    /**
     * Returns true if the system's captive portal probe was blocked for the current default data
     * network. The method will return false if the captive portal probe was not blocked, the login
     * process to the captive portal has been successfully completed, or if the captive portal
     * status can't be determined. Requires ACCESS_NETWORK_STATE permission. Only available on
     * Android Marshmallow and later versions. Returns false on earlier versions.
     */
    @TargetApi(Build.VERSION_CODES.M)
    @CalledByNative
    private static boolean getIsCaptivePortal() {
        // NetworkCapabilities.NET_CAPABILITY_CAPTIVE_PORTAL is only available on Marshmallow and
        // later versions.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return false;
        ConnectivityManager connectivityManager =
                (ConnectivityManager) ContextUtils.getApplicationContext().getSystemService(
                        Context.CONNECTIVITY_SERVICE);
        if (connectivityManager == null) return false;

        Network network = connectivityManager.getActiveNetwork();
        if (network == null) return false;

        NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
        return capabilities != null
                && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_CAPTIVE_PORTAL);
    }

    /**
     * Gets the SSID of the currently associated WiFi access point if there is one. Otherwise,
     * returns empty string.
     */
    @CalledByNative
    public static String getWifiSSID() {
        final Intent intent = ContextUtils.getApplicationContext().registerReceiver(
                null, new IntentFilter(WifiManager.NETWORK_STATE_CHANGED_ACTION));
        if (intent != null) {
            final WifiInfo wifiInfo = intent.getParcelableExtra(WifiManager.EXTRA_WIFI_INFO);
            if (wifiInfo != null) {
                final String ssid = wifiInfo.getSSID();
                if (ssid != null) {
                    return ssid;
                }
            }
        }
        return "";
    }

    public static class NetworkSecurityPolicyProxy {
        private static NetworkSecurityPolicyProxy sInstance = new NetworkSecurityPolicyProxy();

        public static NetworkSecurityPolicyProxy getInstance() {
            return sInstance;
        }

        @VisibleForTesting
        public static void setInstanceForTesting(
                NetworkSecurityPolicyProxy networkSecurityPolicyProxy) {
            sInstance = networkSecurityPolicyProxy;
        }

        @TargetApi(Build.VERSION_CODES.N)
        public boolean isCleartextTrafficPermitted(String host) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) {
                // No per-host configuration before N.
                return isCleartextTrafficPermitted();
            }
            return NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted(host);
        }

        @TargetApi(Build.VERSION_CODES.M)
        public boolean isCleartextTrafficPermitted() {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
                // Always true before M.
                return true;
            }
            return NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted();
        }
    }

    /**
     * Returns true if cleartext traffic to |host| is allowed by the current app.
     */
    @CalledByNative
    private static boolean isCleartextPermitted(String host) {
        try {
            return NetworkSecurityPolicyProxy.getInstance().isCleartextTrafficPermitted(host);
        } catch (IllegalArgumentException e) {
            return NetworkSecurityPolicyProxy.getInstance().isCleartextTrafficPermitted();
        }
    }

    @TargetApi(Build.VERSION_CODES.M)
    @CalledByNative
    private static byte[][] getDnsServers() {
        ConnectivityManager connectivityManager =
                (ConnectivityManager) ContextUtils.getApplicationContext().getSystemService(
                        Context.CONNECTIVITY_SERVICE);
        if (connectivityManager == null) {
            return new byte[0][0];
        }
        Network network = connectivityManager.getActiveNetwork();
        if (network == null) {
            return new byte[0][0];
        }
        LinkProperties linkProperties = connectivityManager.getLinkProperties(network);
        if (linkProperties == null) {
            return new byte[0][0];
        }
        List<InetAddress> dnsServersList = linkProperties.getDnsServers();
        byte[][] dnsServers = new byte[dnsServersList.size()][];
        for (int i = 0; i < dnsServersList.size(); i++) {
            dnsServers[i] = dnsServersList.get(i).getAddress();
        }
        return dnsServers;
    }

    /** Socket that exists only to provide a file descriptor when queried. */
    private static class SocketFd extends Socket {
        /** SocketImpl that exists only to provide a file descriptor when queried. */
        private static class SocketImplFd extends SocketImpl {
            private final ParcelFileDescriptor mPfd;

            /**
             * Create a {@link SocketImpl} that sets {@code fd} as the underlying file descriptor.
             * Does not take ownership of {@code fd}, i.e. {@link #close()} is a no-op.
             */
            SocketImplFd(int fd) {
                mPfd = ParcelFileDescriptor.adoptFd(fd);
                this.fd = mPfd.getFileDescriptor();
            }

            protected void accept(SocketImpl s) {
                throw new RuntimeException("accept not implemented");
            }
            protected int available() {
                throw new RuntimeException("accept not implemented");
            }
            protected void bind(InetAddress host, int port) {
                throw new RuntimeException("accept not implemented");
            }
            protected void close() {
                // Detach from |fd| to avoid leak detection false positives without closing |fd|.
                mPfd.detachFd();
            }
            protected void connect(InetAddress address, int port) {
                throw new RuntimeException("connect not implemented");
            }
            protected void connect(SocketAddress address, int timeout) {
                throw new RuntimeException("connect not implemented");
            }
            protected void connect(String host, int port) {
                throw new RuntimeException("connect not implemented");
            }
            protected void create(boolean stream) {
                throw new RuntimeException("create not implemented");
            }
            protected InputStream getInputStream() {
                throw new RuntimeException("getInputStream not implemented");
            }
            protected OutputStream getOutputStream() {
                throw new RuntimeException("getOutputStream not implemented");
            }
            protected void listen(int backlog) {
                throw new RuntimeException("listen not implemented");
            }
            protected void sendUrgentData(int data) {
                throw new RuntimeException("sendUrgentData not implemented");
            }
            public Object getOption(int optID) {
                throw new RuntimeException("getOption not implemented");
            }
            public void setOption(int optID, Object value) {
                throw new RuntimeException("setOption not implemented");
            }
        }

        /**
         * Creates a {@link Socket} that sets {@code fd} as the underlying file descriptor.
         * Does not take ownership of {@code fd}, i.e. {@link #close()} is a no-op.
         */
        SocketFd(int fd) throws IOException {
            super(new SocketImplFd(fd));
        }
    }

    /**
     * Class to wrap TrafficStats.setThreadStatsUid(int uid) and TrafficStats.clearThreadStatsUid()
     * which are hidden and so must be accessed via reflection.
     */
    private static class ThreadStatsUid {
        // Reference to TrafficStats.setThreadStatsUid(int uid).
        private static final Method sSetThreadStatsUid;
        // Reference to TrafficStats.clearThreadStatsUid().
        private static final Method sClearThreadStatsUid;

        // Get reference to TrafficStats.setThreadStatsUid(int uid) and
        // TrafficStats.clearThreadStatsUid() via reflection.
        static {
            try {
                sSetThreadStatsUid =
                        TrafficStats.class.getMethod("setThreadStatsUid", Integer.TYPE);
                sClearThreadStatsUid = TrafficStats.class.getMethod("clearThreadStatsUid");
            } catch (NoSuchMethodException | SecurityException e) {
                throw new RuntimeException("Unable to get TrafficStats methods", e);
            }
        }

        /** Calls TrafficStats.setThreadStatsUid(uid) */
        public static void set(int uid) throws IllegalAccessException, InvocationTargetException {
            sSetThreadStatsUid.invoke(null, uid); // Pass null for "this" as it's a static method.
        }

        /** Calls TrafficStats.clearThreadStatsUid() */
        public static void clear() throws IllegalAccessException, InvocationTargetException {
            sClearThreadStatsUid.invoke(null); // Pass null for "this" as it's a static method.
        }
    }

    /**
     * Tag socket referenced by {@code fd} with {@code tag} for UID {@code uid}.
     *
     * Assumes thread UID tag isn't set upon entry, and ensures thread UID tag isn't set upon exit.
     * Unfortunately there is no TrafficStatis.getThreadStatsUid().
     */
    @CalledByNative
    private static void tagSocket(int fd, int uid, int tag)
            throws IOException, IllegalAccessException, InvocationTargetException {
        // Set thread tags.
        int oldTag = TrafficStats.getThreadStatsTag();
        if (tag != oldTag) {
            TrafficStats.setThreadStatsTag(tag);
        }
        if (uid != TrafficStatsUid.UNSET) {
            ThreadStatsUid.set(uid);
        }

        // Apply thread tags to socket.
        SocketFd s = new SocketFd(fd);
        TrafficStats.tagSocket(s);
        s.close(); // No-op, just to avoid leak detection false positives.

        // Restore prior thread tags.
        if (tag != oldTag) {
            TrafficStats.setThreadStatsTag(oldTag);
        }
        if (uid != TrafficStatsUid.UNSET) {
            ThreadStatsUid.clear();
        }
    }
}
