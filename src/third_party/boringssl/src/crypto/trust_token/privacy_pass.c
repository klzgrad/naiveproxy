/* Copyright (c) 2020, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include <openssl/bn.h>
#include <openssl/bytestring.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/mem.h>
#include <openssl/nid.h>
#include <openssl/trust_token.h>

#include "../fipsmodule/bn/internal.h"
#include "../fipsmodule/ec/internal.h"


// Privacy Pass uses a custom elliptic curve construction described in
// https://eprint.iacr.org/2020/072.pdf (section 7, construction 4). Ths
// construction provides anonymous tokens with private metadata and validity
// verification.

// get_h returns a randomly selected point for the Privacy Pass protocol.
//
// x: 66591746412783875033873351891229753622964683369847172829242944646280287810
//    81195403447871073952234683395256591180452378091073292247502091640572714366
//    588045092
// y: 12347430519393087872533727997980072129796839266949808299436682045034861065
//    18810630511924722292325611253427311923464047364545304196431830383014967865
//    162306253
//
// This point was generated with the following Python code.

/*
import hashlib

SEED_H = 'PrivacyPass H'

A = -3
B = 0x051953eb9618e1c9a1f929a21a0b68540eea2da725b99b315f3b8b489918ef109e156193951ec7e937b1652c0bd3bb1bf073573df883d2c34f1ef451fd46b503f00
P = 2**521 - 1

def get_y(x):
  y2 = (x**3 + A*x + B) % P
  y = pow(y2, (P+1)/4, P)
  if (y*y) % P != y2:
    raise ValueError("point not on curve")
  return y

def bit(h,i):
  return (ord(h[i/8]) >> (i%8)) & 1

b = 521
def decode_point(so):
  s = hashlib.sha256(so + '0').digest() + hashlib.sha256(so + '1').digest() + \
      hashlib.sha256(so + '2').digest()

  x = 0
  for i in range(0,b):
    x = x + (long(bit(s,i))<<i)
  if x >= P:
    raise ValueError("x out of range")
  y = get_y(x)
  if y & 1 != bit(s,b-1): y = P-y
  return (x, y)


def gen_point(seed):
  v = hashlib.sha256(seed).digest()
  it = 1
  while True:
    try:
      x,y = decode_point(v)
    except Exception, e:
      print e
      it += 1
      v = hashlib.sha256(v).digest()
      continue
    print "Found in %d iterations:" % it
    print "  x = %d" % x
    print "  y = %d" % y
    print " Encoded (hex): (%x, %x)" % (x, y)
    return (x, y)

if __name__ == "__main__":
  gen_point(SEED_H)
*/

static EC_POINT *get_h(void) {
  EC_GROUP *group = EC_GROUP_new_by_curve_name(NID_secp521r1);
  if (group == NULL) {
    return NULL;
  }

  static const BN_ULONG kHGenX[] = {
      TOBN(0x3d01749f, 0xc51e4724),
      TOBN(0x31c28621, 0xf95c98b9),
      TOBN(0x6dc5392a, 0xd4ce846e),
      TOBN(0xda645354, 0x4ef9760d),
      TOBN(0x5945d13e, 0x25337e4c),
      TOBN(0xeb0f6bc0, 0x5c0ecefe),
      TOBN(0xab291003, 0x6f4ef5bd),
      TOBN(0xa9f79ebc, 0x126cefd1),
      0x000001f0,
  };
  static const BIGNUM kX = STATIC_BIGNUM(kHGenX);

  static const BN_ULONG kHGenY[] = {
      TOBN(0xffa6a0ea, 0x966792cd),
      TOBN(0x6e783d17, 0x08e3df3c),
      TOBN(0xb5617012, 0x72ac6ab0),
      TOBN(0xe0bcf350, 0x5c7e6641),
      TOBN(0x53bc55ea, 0xad8f261d),
      TOBN(0xbba93b9d, 0x70491eb4),
      TOBN(0x5214756f, 0x36d9c7fa),
      TOBN(0x1762517d, 0x325e29ac),
      0x0000005c,
  };
  static const BIGNUM kY = STATIC_BIGNUM(kHGenY);

  EC_POINT *h = EC_POINT_new(group);
  if (h == NULL ||
      !EC_POINT_set_affine_coordinates_GFp(group, h, &kX, &kY, NULL)) {
    EC_POINT_free(h);
    return NULL;
  }
  return h;
}

// generate_keypair generates a keypair for the Private Metadata construction.
// |out_x| and |out_y| are set to the secret half of the keypair, while
// |*out_pub| is set to the public half of the keypair. It returns one on
// success and zero on failure.
static int generate_keypair(EC_SCALAR *out_x, EC_SCALAR *out_y,
                            EC_POINT **out_pub, const EC_GROUP *group) {
  EC_POINT *h = get_h();
  if (h == NULL) {
    return 0;
  }

  static const uint8_t kDefaultAdditionalData[32] = {0};
  EC_RAW_POINT tmp1, tmp2;
  EC_POINT *pub = EC_POINT_new(group);
  if (pub == NULL ||
      !ec_random_nonzero_scalar(group, out_x, kDefaultAdditionalData) ||
      !ec_random_nonzero_scalar(group, out_y, kDefaultAdditionalData) ||
      !ec_point_mul_scalar_base(group, &tmp1, out_x) ||
      !ec_point_mul_scalar(group, &tmp2, &h->raw, out_y)) {
    EC_POINT_free(h);
    EC_POINT_free(pub);
    OPENSSL_PUT_ERROR(TRUST_TOKEN, ERR_R_MALLOC_FAILURE);
    return 0;
  }
  group->meth->add(group, &pub->raw, &tmp1, &tmp2);
  *out_pub = pub;

  EC_POINT_free(h);
  return 1;
}

int TRUST_TOKEN_generate_key(uint8_t *out_priv_key, size_t *out_priv_key_len,
                             size_t max_priv_key_len, uint8_t *out_pub_key,
                             size_t *out_pub_key_len, size_t max_pub_key_len,
                             uint32_t id) {
  int ok = 0;
  EC_POINT *pub0 = NULL, *pub1 = NULL, *pubs = NULL;
  CBB cbb;
  CBB_zero(&cbb);
  uint8_t *buf = NULL;
  EC_GROUP *group = EC_GROUP_new_by_curve_name(NID_secp521r1);
  if (group == NULL) {
    return 0;
  }

  EC_SCALAR x0, y0, x1, y1, xs, ys;
  if (!generate_keypair(&x0, &y0, &pub0, group) ||
      !generate_keypair(&x1, &y1, &pub1, group) ||
      !generate_keypair(&xs, &ys, &pubs, group)) {
    OPENSSL_PUT_ERROR(TRUST_TOKEN, TRUST_TOKEN_R_KEYGEN_FAILURE);
    goto err;
  }

  size_t scalar_len = BN_num_bytes(&group->order);
  if (!CBB_init_fixed(&cbb, out_priv_key, max_priv_key_len) ||
      !CBB_add_u32(&cbb, id)) {
    OPENSSL_PUT_ERROR(TRUST_TOKEN, TRUST_TOKEN_R_BUFFER_TOO_SMALL);
    goto err;
  }

  const EC_SCALAR *scalars[] = {&x0, &y0, &x1, &y1, &xs, &ys};
  for (size_t i = 0; i < OPENSSL_ARRAY_SIZE(scalars); i++) {
    if (!CBB_add_space(&cbb, &buf, scalar_len)) {
      OPENSSL_PUT_ERROR(TRUST_TOKEN, TRUST_TOKEN_R_BUFFER_TOO_SMALL);
      goto err;
    }
    ec_scalar_to_bytes(group, buf, &scalar_len, scalars[i]);
  }

  if (!CBB_finish(&cbb, NULL, out_priv_key_len)) {
    OPENSSL_PUT_ERROR(TRUST_TOKEN, TRUST_TOKEN_R_BUFFER_TOO_SMALL);
    goto err;
  }

  CBB pub_cbb;
  if (!CBB_init_fixed(&cbb, out_pub_key, max_pub_key_len) ||
      !CBB_add_u32(&cbb, id) ||
      !CBB_add_u16_length_prefixed(&cbb, &pub_cbb) ||
      !EC_POINT_point2cbb(&pub_cbb, group, pub0, POINT_CONVERSION_UNCOMPRESSED,
                          NULL) ||
      !CBB_add_u16_length_prefixed(&cbb, &pub_cbb) ||
      !EC_POINT_point2cbb(&pub_cbb, group, pub1, POINT_CONVERSION_UNCOMPRESSED,
                          NULL) ||
      !CBB_add_u16_length_prefixed(&cbb, &pub_cbb) ||
      !EC_POINT_point2cbb(&pub_cbb, group, pubs, POINT_CONVERSION_UNCOMPRESSED,
                          NULL) ||
      !CBB_finish(&cbb, NULL, out_pub_key_len)) {
    OPENSSL_PUT_ERROR(TRUST_TOKEN, TRUST_TOKEN_R_BUFFER_TOO_SMALL);
    goto err;
  }

  ok = 1;

err:
  CBB_cleanup(&cbb);
  EC_POINT_free(pub0);
  EC_POINT_free(pub1);
  EC_POINT_free(pubs);
  return ok;
}
