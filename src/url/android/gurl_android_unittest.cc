// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/android/gurl_android.h"
#include "url/gurl.h"
#include "url/native_j_unittests_jni_headers/GURLJavaTest_jni.h"

using base::android::AttachCurrentThread;

namespace url {

class GURLAndroidTest : public ::testing::Test {
 public:
  GURLAndroidTest()
      : j_test_(Java_GURLJavaTest_Constructor(AttachCurrentThread())) {}

  const base::android::ScopedJavaGlobalRef<jobject>& j_test() {
    return j_test_;
  }

 private:
  base::android::ScopedJavaGlobalRef<jobject> j_test_;
};

TEST_F(GURLAndroidTest, TestGURLEquivalence) {
  const char* cases[] = {
      // Common Standard URLs.
      "https://www.google.com",
      "https://www.google.com/",
      "https://www.google.com/maps.htm",
      "https://www.google.com/maps/",
      "https://www.google.com/index.html",
      "https://www.google.com/index.html?q=maps",
      "https://www.google.com/index.html#maps/",
      "https://foo:bar@www.google.com/maps.htm",
      "https://www.google.com/maps/au/index.html",
      "https://www.google.com/maps/au/north",
      "https://www.google.com/maps/au/north/",
      "https://www.google.com/maps/au/index.html?q=maps#fragment/",
      "http://www.google.com:8000/maps/au/index.html?q=maps#fragment/",
      "https://www.google.com/maps/au/north/?q=maps#fragment",
      "https://www.google.com/maps/au/north?q=maps#fragment",
      // Less common standard URLs.
      "filesystem:http://www.google.com/temporary/bar.html?baz=22",
      "file:///temporary/bar.html?baz=22",
      "ftp://foo/test/index.html",
      "gopher://foo/test/index.html",
      "ws://foo/test/index.html",
      // Non-standard,
      "chrome://foo/bar.html",
      "httpa://foo/test/index.html",
      "blob:https://foo.bar/test/index.html",
      "about:blank",
      "data:foobar",
      "scheme:opaque_data",
      // Invalid URLs.
      "foobar",
  };
  JNIEnv* env = AttachCurrentThread();
  for (const char* uri : cases) {
    GURL gurl(uri);
    base::android::ScopedJavaLocalRef<jobject> j_gurl =
        Java_GURLJavaTest_createGURL(
            env, j_test(), base::android::ConvertUTF8ToJavaString(env, uri));
    std::unique_ptr<GURL> gurl2 = GURLAndroid::ToNativeGURL(env, j_gurl);
    EXPECT_EQ(gurl, *gurl2);
  }
}

JAVA_TESTS(GURLAndroidTest, j_test())

}  // namespace url
