// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.url;

import org.junit.Assert;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.CalledByNativeJavaTest;

import java.net.URISyntaxException;

/**
 * Tests for {@link GURL}. GURL relies heavily on the native implementation, and the lion's share of
 * the logic is tested there. This test is primarily to make sure everything is plumbed through
 * correctly.
 */
public class GURLJavaTest {
    @CalledByNative
    private GURLJavaTest() {}

    @CalledByNative
    public GURL createGURL(String uri) {
        return new GURL(uri);
    }

    // Equivalent of GURLTest.Components
    @CalledByNativeJavaTest
    @SuppressWarnings(value = "AuthLeak")
    public void testComponents() {
        GURL empty = new GURL("");
        Assert.assertTrue(empty.isEmpty());
        Assert.assertFalse(empty.isValid());

        GURL url = new GURL("http://user:pass@google.com:99/foo;bar?q=a#ref");
        Assert.assertFalse(url.isEmpty());
        Assert.assertTrue(url.isValid());
        Assert.assertTrue(url.getScheme().equals("http"));

        Assert.assertEquals("http://user:pass@google.com:99/foo;bar?q=a#ref", url.getSpec());

        Assert.assertEquals("http", url.getScheme());
        Assert.assertEquals("user", url.getUsername());
        Assert.assertEquals("pass", url.getPassword());
        Assert.assertEquals("google.com", url.getHost());
        Assert.assertEquals("99", url.getPort());
        Assert.assertEquals("/foo;bar", url.getPath());
        Assert.assertEquals("q=a", url.getQuery());
        Assert.assertEquals("ref", url.getRef());

        // Test parsing userinfo with special characters.
        GURL urlSpecialPass = new GURL("http://user:%40!$&'()*+,;=:@google.com:12345");
        Assert.assertTrue(urlSpecialPass.isValid());
        // GURL canonicalizes some delimiters.
        Assert.assertEquals("%40!$&%27()*+,%3B%3D%3A", urlSpecialPass.getPassword());
        Assert.assertEquals("google.com", urlSpecialPass.getHost());
        Assert.assertEquals("12345", urlSpecialPass.getPort());
    }

    // Equivalent of GURLTest.Empty
    @CalledByNativeJavaTest
    public void testEmpty() {
        GURL url = new GURL("");
        Assert.assertFalse(url.isValid());
        Assert.assertEquals("", url.getSpec());

        Assert.assertEquals("", url.getScheme());
        Assert.assertEquals("", url.getUsername());
        Assert.assertEquals("", url.getPassword());
        Assert.assertEquals("", url.getHost());
        Assert.assertEquals("", url.getPort());
        Assert.assertEquals("", url.getPath());
        Assert.assertEquals("", url.getQuery());
        Assert.assertEquals("", url.getRef());
    }

    // Test that GURL and URI return the correct Origin.
    @CalledByNativeJavaTest
    @SuppressWarnings(value = "AuthLeak")
    public void testOrigin() throws URISyntaxException {
        final String kExpectedOrigin1 = "http://google.com:21/";
        final String kExpectedOrigin2 = "";
        GURL url1 = new GURL("filesystem:http://user:pass@google.com:21/blah#baz");
        GURL url2 = new GURL("javascript:window.alert(\"hello,world\");");
        URI uri = new URI("filesystem:http://user:pass@google.com:21/blah#baz");

        Assert.assertEquals(url1.getOrigin().getSpec(), kExpectedOrigin1);
        Assert.assertEquals(url2.getOrigin().getSpec(), kExpectedOrigin2);
        URI origin = uri.getOrigin();
        Assert.assertEquals(origin.getSpec(), kExpectedOrigin1);
    }

    @CalledByNativeJavaTest
    public void testWideInput() throws URISyntaxException {
        final String kExpectedSpec = "http://xn--1xa.com/";

        GURL url = new GURL("http://\u03C0.com");
        Assert.assertEquals("http://xn--1xa.com/", url.getSpec());
        Assert.assertEquals("http", url.getScheme());
        Assert.assertEquals("", url.getUsername());
        Assert.assertEquals("", url.getPassword());
        Assert.assertEquals("xn--1xa.com", url.getHost());
        Assert.assertEquals("", url.getPort());
        Assert.assertEquals("/", url.getPath());
        Assert.assertEquals("", url.getQuery());
        Assert.assertEquals("", url.getRef());
    }
}
