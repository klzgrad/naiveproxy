#!/bin/sh

set -ex

DEBIAN_TAG_VERSION=115.0.5790.170-1
url=https://salsa.debian.org/chromium-team/chromium/-/archive/debian/$DEBIAN_TAG_VERSION/chromium-debian-$DEBIAN_TAG_VERSION.tar.gz

rm -rf chromium-debian-$DEBIAN_TAG_VERSION
trap "rm -rf chromium-debian-$DEBIAN_TAG_VERSION" EXIT
curl -L $url -o- | tar xzf -

series="
third_party/0001-Add-PPC64-support-for-boringssl.patch
third_party/0001-third_party-boringssl-Properly-detect-ppc64le-in-BUI.patch
third_party/0001-third_party-lss-Don-t-look-for-mmap2-on-ppc64.patch
third_party/0002-third_party-lss-kernel-structs.patch
third_party/0002-third-party-boringssl-add-generated-files.patch
third_party/use-sysconf-page-size-on-ppc64.patch
"
for i in $series; do
  patch --batch -p1 <chromium-debian-$DEBIAN_TAG_VERSION/debian/patches/ppc64le/$i || true
done

patch -p2 <<EOF
diff --git a/src/third_party/boringssl/src/crypto/fipsmodule/modes/internal.h b/src/third_party/boringssl/src/crypto/fipsmodule/modes/internal.h
index ecda045c59..8780f7e3d6 100644
--- a/src/third_party/boringssl/src/crypto/fipsmodule/modes/internal.h
+++ b/src/third_party/boringssl/src/crypto/fipsmodule/modes/internal.h
@@ -329,8 +329,8 @@ void aes_gcm_dec_kernel(const uint8_t *in, uint64_t in_bits, void *out,
 #define GHASH_ASM_PPC64LE
 #define GCM_FUNCREF
 void gcm_init_p8(u128 Htable[16], const uint64_t Xi[2]);
-void gcm_gmult_p8(uint64_t Xi[2], const u128 Htable[16]);
-void gcm_ghash_p8(uint64_t Xi[2], const u128 Htable[16], const uint8_t *inp,
+void gcm_gmult_p8(uint8_t Xi[16], const u128 Htable[16]);
+void gcm_ghash_p8(uint8_t Xi[16], const u128 Htable[16], const uint8_t *inp,
                   size_t len);
 #endif
 #endif  // OPENSSL_NO_ASM
EOF
