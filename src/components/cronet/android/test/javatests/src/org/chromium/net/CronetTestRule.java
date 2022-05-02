// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.content.Context;
import android.os.StrictMode;
import android.support.test.InstrumentationRegistry;

import org.junit.Assert;
import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.PathUtils;
import org.chromium.net.impl.JavaCronetEngine;
import org.chromium.net.impl.JavaCronetProvider;
import org.chromium.net.impl.UserAgent;

import java.io.File;
import java.lang.annotation.Annotation;
import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import java.net.URL;
import java.net.URLStreamHandlerFactory;

/**
 * Custom TestRule for Cronet instrumentation tests.
 *
 * TODO(yolandyan): refactor this to three TestRules, one for setUp and tearDown,
 * one for tests under org.chromium.net.urlconnection, one for test under
 * org.chromium.net
 */
public class CronetTestRule implements TestRule {
    private static final String PRIVATE_DATA_DIRECTORY_SUFFIX = "cronet_test";

    private CronetTestFramework mCronetTestFramework;

    // {@code true} when test is being run against system HttpURLConnection implementation.
    private boolean mTestingSystemHttpURLConnection;
    private boolean mTestingJavaImpl;
    private StrictMode.VmPolicy mOldVmPolicy;

    /**
     * Name of the file that contains the test server certificate in PEM format.
     */
    public static final String SERVER_CERT_PEM = "quic-chain.pem";

    /**
     * Name of the file that contains the test server private key in PKCS8 PEM format.
     */
    public static final String SERVER_KEY_PKCS8_PEM = "quic-leaf-cert.key.pkcs8.pem";

    private static final String TAG = CronetTestRule.class.getSimpleName();

    /**
     * Creates and holds pointer to CronetEngine.
     */
    public static class CronetTestFramework {
        public ExperimentalCronetEngine mCronetEngine;

        public CronetTestFramework(Context context) {
            mCronetEngine = new ExperimentalCronetEngine.Builder(context).enableQuic(true).build();
            // Start collecting metrics.
            mCronetEngine.getGlobalMetricsDeltas();
        }
    }

    int getMaximumAvailableApiLevel() {
        // Prior to M59 the ApiVersion.getMaximumAvailableApiLevel API didn't exist
        if (ApiVersion.getCronetVersion().compareTo("59") < 0) {
            return 3;
        }
        return ApiVersion.getMaximumAvailableApiLevel();
    }

    public static Context getContext() {
        return InstrumentationRegistry.getTargetContext();
    }

    @Override
    public Statement apply(final Statement base, final Description desc) {
        return new Statement() {
            @Override
            public void evaluate() throws Throwable {
                setUp();
                runBase(base, desc);
                tearDown();
            }
        };
    }

    /**
     * Returns {@code true} when test is being run against system HttpURLConnection implementation.
     */
    public boolean testingSystemHttpURLConnection() {
        return mTestingSystemHttpURLConnection;
    }

    /**
     * Returns {@code true} when test is being run against the java implementation of CronetEngine.
     */
    public boolean testingJavaImpl() {
        return mTestingJavaImpl;
    }

    // TODO(yolandyan): refactor this using parameterize framework
    private void runBase(Statement base, Description desc) throws Throwable {
        setTestingSystemHttpURLConnection(false);
        setTestingJavaImpl(false);
        String packageName = desc.getTestClass().getPackage().getName();

        // Find the API version required by the test.
        int requiredApiVersion = getMaximumAvailableApiLevel();
        for (Annotation a : desc.getTestClass().getAnnotations()) {
            if (a instanceof RequiresMinApi) {
                requiredApiVersion = ((RequiresMinApi) a).value();
            }
        }
        for (Annotation a : desc.getAnnotations()) {
            if (a instanceof RequiresMinApi) {
                // Method scoped requirements take precedence over class scoped
                // requirements.
                requiredApiVersion = ((RequiresMinApi) a).value();
            }
        }

        if (requiredApiVersion > getMaximumAvailableApiLevel()) {
            Log.i(TAG,
                    desc.getMethodName() + " skipped because it requires API " + requiredApiVersion
                            + " but only API " + getMaximumAvailableApiLevel() + " is present.");
        } else if (packageName.equals("org.chromium.net.urlconnection")) {
            try {
                if (desc.getAnnotation(CompareDefaultWithCronet.class) != null) {
                    // Run with the default HttpURLConnection implementation first.
                    setTestingSystemHttpURLConnection(true);
                    base.evaluate();
                    // Use Cronet's implementation, and run the same test.
                    setTestingSystemHttpURLConnection(false);
                    base.evaluate();
                } else {
                    // For all other tests.
                    base.evaluate();
                }
            } catch (Throwable e) {
                throw new Throwable("Cronet Test failed.", e);
            }
        } else if (packageName.equals("org.chromium.net")) {
            try {
                base.evaluate();
                if (desc.getAnnotation(OnlyRunNativeCronet.class) == null) {
                    setTestingJavaImpl(true);
                    base.evaluate();
                }
            } catch (Throwable e) {
                throw new Throwable("CronetTestBase#runTest failed.", e);
            }
        } else {
            base.evaluate();
        }
    }

    void setUp() throws Exception {
        System.loadLibrary("cronet_tests");
        ContextUtils.initApplicationContext(getContext().getApplicationContext());
        PathUtils.setPrivateDataDirectorySuffix(PRIVATE_DATA_DIRECTORY_SUFFIX);
        prepareTestStorage(getContext());
        mOldVmPolicy = StrictMode.getVmPolicy();
        // Only enable StrictMode testing after leaks were fixed in crrev.com/475945
        if (getMaximumAvailableApiLevel() >= 7) {
            StrictMode.setVmPolicy(new StrictMode.VmPolicy.Builder()
                                           .detectLeakedClosableObjects()
                                           .penaltyLog()
                                           .penaltyDeath()
                                           .build());
        }
    }

    void tearDown() throws Exception {
        try {
            // Run GC and finalizers a few times to pick up leaked closeables
            for (int i = 0; i < 10; i++) {
                System.gc();
                System.runFinalization();
            }
            System.gc();
            System.runFinalization();
        } finally {
            StrictMode.setVmPolicy(mOldVmPolicy);
        }
    }

    /**
     * Starts the CronetTest framework.
     */
    public CronetTestFramework startCronetTestFramework() {
        mCronetTestFramework = new CronetTestFramework(getContext());
        if (testingJavaImpl()) {
            ExperimentalCronetEngine.Builder builder = createJavaEngineBuilder();
            builder.setUserAgent(UserAgent.from(getContext()));
            mCronetTestFramework.mCronetEngine = builder.build();
            // Make sure that the instantiated engine is JavaCronetEngine.
            assert mCronetTestFramework.mCronetEngine.getClass() == JavaCronetEngine.class;
        }
        return mCronetTestFramework;
    }

    /**
     * Creates and returns {@link ExperimentalCronetEngine.Builder} that creates
     * Java (platform) based {@link CronetEngine.Builder}.
     *
     * @return the {@code CronetEngine.Builder} that builds Java-based {@code Cronet engine}.
     */
    public ExperimentalCronetEngine.Builder createJavaEngineBuilder() {
        return (ExperimentalCronetEngine.Builder) new JavaCronetProvider(getContext())
                .createBuilder();
    }

    public void assertResponseEquals(UrlResponseInfo expected, UrlResponseInfo actual) {
        Assert.assertEquals(expected.getAllHeaders(), actual.getAllHeaders());
        Assert.assertEquals(expected.getAllHeadersAsList(), actual.getAllHeadersAsList());
        Assert.assertEquals(expected.getHttpStatusCode(), actual.getHttpStatusCode());
        Assert.assertEquals(expected.getHttpStatusText(), actual.getHttpStatusText());
        Assert.assertEquals(expected.getUrlChain(), actual.getUrlChain());
        Assert.assertEquals(expected.getUrl(), actual.getUrl());
        // Transferred bytes and proxy server are not supported in pure java
        if (!testingJavaImpl()) {
            Assert.assertEquals(expected.getReceivedByteCount(), actual.getReceivedByteCount());
            Assert.assertEquals(expected.getProxyServer(), actual.getProxyServer());
            // This is a place where behavior intentionally differs between native and java
            Assert.assertEquals(expected.getNegotiatedProtocol(), actual.getNegotiatedProtocol());
        }
    }

    public static void assertContains(String expectedSubstring, String actualString) {
        Assert.assertNotNull(actualString);
        if (!actualString.contains(expectedSubstring)) {
            Assert.fail("String [" + actualString + "] doesn't contain substring ["
                    + expectedSubstring + "]");
        }
    }

    public CronetEngine.Builder enableDiskCache(CronetEngine.Builder cronetEngineBuilder) {
        cronetEngineBuilder.setStoragePath(getTestStorage(getContext()));
        cronetEngineBuilder.enableHttpCache(CronetEngine.Builder.HTTP_CACHE_DISK, 1000 * 1024);
        return cronetEngineBuilder;
    }

    /**
     * Sets the {@link URLStreamHandlerFactory} from {@code cronetEngine}.  This should be called
     * during setUp() and is installed by {@link runTest()} as the default when Cronet is tested.
     */
    public void setStreamHandlerFactory(CronetEngine cronetEngine) {
        if (!testingSystemHttpURLConnection()) {
            URL.setURLStreamHandlerFactory(cronetEngine.createURLStreamHandlerFactory());
        }
    }

    /**
     * Annotation for test methods in org.chromium.net.urlconnection pacakage that runs them
     * against both Cronet's HttpURLConnection implementation, and against the system's
     * HttpURLConnection implementation.
     */
    @Target(ElementType.METHOD)
    @Retention(RetentionPolicy.RUNTIME)
    public @interface CompareDefaultWithCronet {}

    /**
     * Annotation for test methods in org.chromium.net.urlconnection pacakage that runs them
     * only against Cronet's HttpURLConnection implementation, and not against the system's
     * HttpURLConnection implementation.
     */
    @Target(ElementType.METHOD)
    @Retention(RetentionPolicy.RUNTIME)
    public @interface OnlyRunCronetHttpURLConnection {}

    /**
     * Annotation for test methods in org.chromium.net package that disables rerunning the test
     * against the Java-only implementation. When this annotation is present the test is only run
     * against the native implementation.
     */
    @Target(ElementType.METHOD)
    @Retention(RetentionPolicy.RUNTIME)
    public @interface OnlyRunNativeCronet {}

    /**
     * Annotation allowing classes or individual tests to be skipped based on the version of the
     * Cronet API present. Takes the minimum API version upon which the test should be run.
     * For example if a test should only be run with API version 2 or greater:
     *   @RequiresMinApi(2)
     *   public void testFoo() {}
     */
    @Target({ElementType.TYPE, ElementType.METHOD})
    @Retention(RetentionPolicy.RUNTIME)
    public @interface RequiresMinApi {
        int value();
    }
    /**
     * Prepares the path for the test storage (http cache, QUIC server info).
     */
    public static void prepareTestStorage(Context context) {
        File storage = new File(getTestStorageDirectory());
        if (storage.exists()) {
            Assert.assertTrue(recursiveDelete(storage));
        }
        ensureTestStorageExists();
    }

    /**
     * Returns the path for the test storage (http cache, QUIC server info).
     * Also ensures it exists.
     */
    public static String getTestStorage(Context context) {
        ensureTestStorageExists();
        return getTestStorageDirectory();
    }

    /**
     * Returns the path for the test storage (http cache, QUIC server info).
     * NOTE: Does not ensure it exists; tests should use {@link #getTestStorage}.
     */
    private static String getTestStorageDirectory() {
        return PathUtils.getDataDirectory() + "/test_storage";
    }

    /**
     * Ensures test storage directory exists, i.e. creates one if it does not exist.
     */
    private static void ensureTestStorageExists() {
        File storage = new File(getTestStorageDirectory());
        if (!storage.exists()) {
            Assert.assertTrue(storage.mkdir());
        }
    }

    private static boolean recursiveDelete(File path) {
        if (path.isDirectory()) {
            for (File c : path.listFiles()) {
                if (!recursiveDelete(c)) {
                    return false;
                }
            }
        }
        return path.delete();
    }

    private void setTestingSystemHttpURLConnection(boolean value) {
        mTestingSystemHttpURLConnection = value;
    }

    private void setTestingJavaImpl(boolean value) {
        mTestingJavaImpl = value;
    }
}
