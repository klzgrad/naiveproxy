/********************************************************************************************
* SIDH: an efficient supersingular isogeny cryptography library
*
* Abstract: supersingular isogeny key encapsulation (SIKE) protocol
*********************************************************************************************/

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <openssl/bn.h>
#include <openssl/base.h>
#include <openssl/rand.h>
#include <openssl/mem.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "utils.h"
#include "isogeny.h"
#include "fpx.h"

extern const struct params_t p503;

// Domain separation parameters for HMAC
static const uint8_t G[2] = {0,0};
static const uint8_t H[2] = {1,0};
static const uint8_t F[2] = {2,0};

// SIDHp503_JINV_BYTESZ is a number of bytes used for encoding j-invariant.
#define SIDHp503_JINV_BYTESZ    126U
// SIDHp503_PRV_A_BITSZ is a number of bits of SIDH private key (2-isogeny)
#define SIDHp503_PRV_A_BITSZ    250U
// SIDHp503_PRV_A_BITSZ is a number of bits of SIDH private key (3-isogeny)
#define SIDHp503_PRV_B_BITSZ    253U
// MAX_INT_POINTS_ALICE is a number of points used in 2-isogeny tree computation
#define MAX_INT_POINTS_ALICE    7U
// MAX_INT_POINTS_ALICE is a number of points used in 3-isogeny tree computation
#define MAX_INT_POINTS_BOB      8U

// Produces HMAC-SHA256 of data |S| mac'ed with the key |key|. Result is stored in |out|
// which must have size of at least |outsz| bytes and must be not bigger than
// SHA256_DIGEST_LENGTH. The output of a HMAC may be truncated.
// The |key| buffer is reused by the hmac_sum and hence, it's size must be equal
// to SHA256_CBLOCK. The HMAC key provided in |key| buffer must be smaller or equal
// to SHA256_DIGHEST_LENTH. |key| can overlap |out|.
static void hmac_sum(
    uint8_t *out, size_t outsz, const uint8_t S[2], uint8_t key[SHA256_CBLOCK]) {
    for(size_t i=0; i<SHA256_DIGEST_LENGTH; i++) {
        key[i] = key[i] ^ 0x36;
    }
    // set rest of the buffer to ipad = 0x36
    memset(&key[SHA256_DIGEST_LENGTH], 0x36, SHA256_CBLOCK - SHA256_DIGEST_LENGTH);

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, key, SHA256_CBLOCK);
    SHA256_Update(&ctx, S, 2);
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);

    // XOR key with an opad = 0x5C
    for(size_t i=0; i<SHA256_CBLOCK; i++) {
        key[i] = key[i] ^ 0x36 ^ 0x5C;
    }

    SHA256_Init(&ctx);
    SHA256_Update(&ctx, key, SHA256_CBLOCK);
    SHA256_Update(&ctx, digest, SHA256_DIGEST_LENGTH);
    SHA256_Final(digest, &ctx);
    assert(outsz <= sizeof(digest));
    memcpy(out, digest, outsz);
}

// Swap points.
// If option = 0 then P <- P and Q <- Q, else if option = 0xFF...FF then P <- Q and Q <- P
#if !defined(OPENSSL_X86_64) || defined(OPENSSL_NO_ASM)
static void sike_cswap(point_proj_t P, point_proj_t Q, const crypto_word_t option)
{
    crypto_word_t temp;
    for (size_t i = 0; i < NWORDS_FIELD; i++) {
        temp = option & (P->X->c0[i] ^ Q->X->c0[i]);
        P->X->c0[i] = temp ^ P->X->c0[i];
        Q->X->c0[i] = temp ^ Q->X->c0[i];
        temp = option & (P->Z->c0[i] ^ Q->Z->c0[i]);
        P->Z->c0[i] = temp ^ P->Z->c0[i];
        Q->Z->c0[i] = temp ^ Q->Z->c0[i];
        temp = option & (P->X->c1[i] ^ Q->X->c1[i]);
        P->X->c1[i] = temp ^ P->X->c1[i];
        Q->X->c1[i] = temp ^ Q->X->c1[i];
        temp = option & (P->Z->c1[i] ^ Q->Z->c1[i]);
        P->Z->c1[i] = temp ^ P->Z->c1[i];
        Q->Z->c1[i] = temp ^ Q->Z->c1[i];
    }
}
#endif

// Swap points.
// If option = 0 then P <- P and Q <- Q, else if option = 0xFF...FF then P <- Q and Q <- P
static inline void sike_fp2cswap(point_proj_t P, point_proj_t Q, const crypto_word_t option)
{
#if defined(OPENSSL_X86_64) && !defined(OPENSSL_NO_ASM)
    sike_cswap_asm(P, Q, option);
#else
    sike_cswap(P, Q, option);
#endif
}

static void LADDER3PT(
    const f2elm_t xP, const f2elm_t xQ, const f2elm_t xPQ, const uint8_t* m,
    int is_A, point_proj_t R, const f2elm_t A) {
    point_proj_t R0 = POINT_PROJ_INIT, R2 = POINT_PROJ_INIT;
    f2elm_t A24 = F2ELM_INIT;
    crypto_word_t mask;
    int bit, swap, prevbit = 0;

    const size_t nbits = is_A?SIDHp503_PRV_A_BITSZ:SIDHp503_PRV_B_BITSZ;

    // Initializing constant
    sike_fpcopy(p503.mont_one, A24[0].c0);
    sike_fp2add(A24, A24, A24);
    sike_fp2add(A, A24, A24);
    sike_fp2div2(A24, A24);
    sike_fp2div2(A24, A24); // A24 = (A+2)/4

    // Initializing points
    sike_fp2copy(xQ, R0->X);
    sike_fpcopy(p503.mont_one, R0->Z[0].c0);
    sike_fp2copy(xPQ, R2->X);
    sike_fpcopy(p503.mont_one, R2->Z[0].c0);
    sike_fp2copy(xP, R->X);
    sike_fpcopy(p503.mont_one, R->Z[0].c0);
    memset(R->Z->c1, 0, sizeof(R->Z->c1));

    // Main loop
    for (size_t i = 0; i < nbits; i++) {
        bit = (m[i >> 3] >> (i & 7)) & 1;
        swap = bit ^ prevbit;
        prevbit = bit;
        mask = 0 - (crypto_word_t)swap;

        sike_fp2cswap(R, R2, mask);
        xDBLADD(R0, R2, R->X, A24);
        sike_fp2mul_mont(R2->X, R->Z, R2->X);
    }
}

// Initialization of basis points
static inline void sike_init_basis(const crypto_word_t *gen, f2elm_t XP, f2elm_t XQ, f2elm_t XR) {
    sike_fpcopy(gen,                  XP->c0);
    sike_fpcopy(gen +   NWORDS_FIELD, XP->c1);
    sike_fpcopy(gen + 2*NWORDS_FIELD, XQ->c0);
    memset(XQ->c1, 0, sizeof(XQ->c1));
    sike_fpcopy(gen + 3*NWORDS_FIELD, XR->c0);
    sike_fpcopy(gen + 4*NWORDS_FIELD, XR->c1);
}

// Conversion of GF(p^2) element from Montgomery to standard representation.
static inline void sike_fp2_encode(const f2elm_t x, uint8_t *enc) {
    f2elm_t t;
    sike_from_fp2mont(x, t);

    // convert to bytes in little endian form
    for (size_t i=0; i<FIELD_BYTESZ; i++) {
        enc[i+           0] = (t[0].c0[i/LSZ] >> (8*(i%LSZ))) & 0xFF;
        enc[i+FIELD_BYTESZ] = (t[0].c1[i/LSZ] >> (8*(i%LSZ))) & 0xFF;
    }
}

// Parse byte sequence back into GF(p^2) element, and conversion to Montgomery representation.
// Elements over GF(p503) are encoded in 63 octets in little endian format
// (i.e., the least significant octet is located in the lowest memory address).
static inline void fp2_decode(const uint8_t *enc, f2elm_t t) {
    memset(t[0].c0, 0, sizeof(t[0].c0));
    memset(t[0].c1, 0, sizeof(t[0].c1));
    // convert bytes in little endian form to f2elm_t
    for (size_t i = 0; i < FIELD_BYTESZ; i++) {
        t[0].c0[i/LSZ] |= ((crypto_word_t)enc[i+           0]) << (8*(i%LSZ));
        t[0].c1[i/LSZ] |= ((crypto_word_t)enc[i+FIELD_BYTESZ]) << (8*(i%LSZ));
    }
    sike_to_fp2mont(t, t);
}

// Alice's ephemeral public key generation
// Input:  a private key prA in the range [0, 2^250 - 1], stored in 32 bytes.
// Output: the public key pkA consisting of 3 GF(p503^2) elements encoded in 378 bytes.
static void gen_iso_A(const uint8_t* skA, uint8_t* pkA)
{
    point_proj_t R, pts[MAX_INT_POINTS_ALICE];
    point_proj_t phiP = POINT_PROJ_INIT;
    point_proj_t phiQ = POINT_PROJ_INIT;
    point_proj_t phiR = POINT_PROJ_INIT;
    f2elm_t XPA, XQA, XRA, coeff[3];
    f2elm_t A24plus = F2ELM_INIT;
    f2elm_t C24 = F2ELM_INIT;
    f2elm_t A = F2ELM_INIT;
    unsigned int m, index = 0, pts_index[MAX_INT_POINTS_ALICE], npts = 0, ii = 0;

    // Initialize basis points
    sike_init_basis(p503.A_gen, XPA, XQA, XRA);
    sike_init_basis(p503.B_gen, phiP->X, phiQ->X, phiR->X);
    sike_fpcopy(p503.mont_one, (phiP->Z)->c0);
    sike_fpcopy(p503.mont_one, (phiQ->Z)->c0);
    sike_fpcopy(p503.mont_one, (phiR->Z)->c0);

    // Initialize constants
    sike_fpcopy(p503.mont_one, A24plus->c0);
    sike_fp2add(A24plus, A24plus, C24);

    // Retrieve kernel point
    LADDER3PT(XPA, XQA, XRA, skA, 1, R, A);

    // Traverse tree
    index = 0;
    for (size_t row = 1; row < A_max; row++) {
        while (index < A_max-row) {
            sike_fp2copy(R->X, pts[npts]->X);
            sike_fp2copy(R->Z, pts[npts]->Z);
            pts_index[npts++] = index;
            m = p503.A_strat[ii++];
            xDBLe(R, R, A24plus, C24, (2*m));
            index += m;
        }
        get_4_isog(R, A24plus, C24, coeff);

        for (size_t i = 0; i < npts; i++) {
            eval_4_isog(pts[i], coeff);
        }
        eval_4_isog(phiP, coeff);
        eval_4_isog(phiQ, coeff);
        eval_4_isog(phiR, coeff);

        sike_fp2copy(pts[npts-1]->X, R->X);
        sike_fp2copy(pts[npts-1]->Z, R->Z);
        index = pts_index[npts-1];
        npts -= 1;
    }

    get_4_isog(R, A24plus, C24, coeff);
    eval_4_isog(phiP, coeff);
    eval_4_isog(phiQ, coeff);
    eval_4_isog(phiR, coeff);

    inv_3_way(phiP->Z, phiQ->Z, phiR->Z);
    sike_fp2mul_mont(phiP->X, phiP->Z, phiP->X);
    sike_fp2mul_mont(phiQ->X, phiQ->Z, phiQ->X);
    sike_fp2mul_mont(phiR->X, phiR->Z, phiR->X);

    // Format public key
    sike_fp2_encode(phiP->X, pkA);
    sike_fp2_encode(phiQ->X, pkA + SIDHp503_JINV_BYTESZ);
    sike_fp2_encode(phiR->X, pkA + 2*SIDHp503_JINV_BYTESZ);
}

// Bob's ephemeral key-pair generation
// It produces a private key skB and computes the public key pkB.
// The private key is an integer in the range [0, 2^Floor(Log(2,3^159)) - 1], stored in 32 bytes.
// The public key consists of 3 GF(p503^2) elements encoded in 378 bytes.
static void gen_iso_B(const uint8_t* skB, uint8_t* pkB)
{
    point_proj_t R, pts[MAX_INT_POINTS_BOB];
    point_proj_t phiP = POINT_PROJ_INIT;
    point_proj_t phiQ = POINT_PROJ_INIT;
    point_proj_t phiR = POINT_PROJ_INIT;
    f2elm_t XPB, XQB, XRB, coeff[3];
    f2elm_t A24plus = F2ELM_INIT;
    f2elm_t A24minus = F2ELM_INIT;
    f2elm_t A = F2ELM_INIT;
    unsigned int m, index = 0, pts_index[MAX_INT_POINTS_BOB], npts = 0, ii = 0;

    // Initialize basis points
    sike_init_basis(p503.B_gen, XPB, XQB, XRB);
    sike_init_basis(p503.A_gen, phiP->X, phiQ->X, phiR->X);
    sike_fpcopy(p503.mont_one, (phiP->Z)->c0);
    sike_fpcopy(p503.mont_one, (phiQ->Z)->c0);
    sike_fpcopy(p503.mont_one, (phiR->Z)->c0);

    // Initialize constants
    sike_fpcopy(p503.mont_one, A24plus->c0);
    sike_fp2add(A24plus, A24plus, A24plus);
    sike_fp2copy(A24plus, A24minus);
    sike_fp2neg(A24minus);

    // Retrieve kernel point
    LADDER3PT(XPB, XQB, XRB, skB, 0, R, A);

    // Traverse tree
    index = 0;
    for (size_t row = 1; row < B_max; row++) {
        while (index < B_max-row) {
            sike_fp2copy(R->X, pts[npts]->X);
            sike_fp2copy(R->Z, pts[npts]->Z);
            pts_index[npts++] = index;
            m = p503.B_strat[ii++];
            xTPLe(R, R, A24minus, A24plus, m);
            index += m;
        }
        get_3_isog(R, A24minus, A24plus, coeff);

        for (size_t i = 0; i < npts; i++) {
            eval_3_isog(pts[i], coeff);
        }
        eval_3_isog(phiP, coeff);
        eval_3_isog(phiQ, coeff);
        eval_3_isog(phiR, coeff);

        sike_fp2copy(pts[npts-1]->X, R->X);
        sike_fp2copy(pts[npts-1]->Z, R->Z);
        index = pts_index[npts-1];
        npts -= 1;
    }

    get_3_isog(R, A24minus, A24plus, coeff);
    eval_3_isog(phiP, coeff);
    eval_3_isog(phiQ, coeff);
    eval_3_isog(phiR, coeff);

    inv_3_way(phiP->Z, phiQ->Z, phiR->Z);
    sike_fp2mul_mont(phiP->X, phiP->Z, phiP->X);
    sike_fp2mul_mont(phiQ->X, phiQ->Z, phiQ->X);
    sike_fp2mul_mont(phiR->X, phiR->Z, phiR->X);

    // Format public key
    sike_fp2_encode(phiP->X, pkB);
    sike_fp2_encode(phiQ->X, pkB + SIDHp503_JINV_BYTESZ);
    sike_fp2_encode(phiR->X, pkB + 2*SIDHp503_JINV_BYTESZ);
}

// Alice's ephemeral shared secret computation
// It produces a shared secret key ssA using her secret key skA and Bob's public key pkB
// Inputs: Alice's skA is an integer in the range [0, 2^250 - 1], stored in 32 bytes.
//         Bob's pkB consists of 3 GF(p503^2) elements encoded in 378 bytes.
// Output: a shared secret ssA that consists of one element in GF(p503^2) encoded in 126 bytes.
static void ex_iso_A(const uint8_t* skA, const uint8_t* pkB, uint8_t* ssA)
{
    point_proj_t R, pts[MAX_INT_POINTS_ALICE];
    f2elm_t coeff[3], PKB[3], jinv;
    f2elm_t A24plus = F2ELM_INIT;
    f2elm_t C24 = F2ELM_INIT;
    f2elm_t A = F2ELM_INIT;
    unsigned int m, index = 0, pts_index[MAX_INT_POINTS_ALICE], npts = 0, ii = 0;

    // Initialize images of Bob's basis
    fp2_decode(pkB, PKB[0]);
    fp2_decode(pkB + SIDHp503_JINV_BYTESZ, PKB[1]);
    fp2_decode(pkB + 2*SIDHp503_JINV_BYTESZ, PKB[2]);

    // Initialize constants
    get_A(PKB[0], PKB[1], PKB[2], A); // TODO: Can return projective A?
    sike_fpadd(p503.mont_one, p503.mont_one, C24->c0);
    sike_fp2add(A, C24, A24plus);
    sike_fpadd(C24->c0, C24->c0, C24->c0);

    // Retrieve kernel point
    LADDER3PT(PKB[0], PKB[1], PKB[2], skA, 1, R, A);

    // Traverse tree
    index = 0;
    for (size_t row = 1; row < A_max; row++) {
        while (index < A_max-row) {
            sike_fp2copy(R->X, pts[npts]->X);
            sike_fp2copy(R->Z, pts[npts]->Z);
            pts_index[npts++] = index;
            m = p503.A_strat[ii++];
            xDBLe(R, R, A24plus, C24, (2*m));
            index += m;
        }
        get_4_isog(R, A24plus, C24, coeff);

        for (size_t i = 0; i < npts; i++) {
            eval_4_isog(pts[i], coeff);
        }

        sike_fp2copy(pts[npts-1]->X, R->X);
        sike_fp2copy(pts[npts-1]->Z, R->Z);
        index = pts_index[npts-1];
        npts -= 1;
    }

    get_4_isog(R, A24plus, C24, coeff);
    sike_fp2div2(C24, C24);
    sike_fp2sub(A24plus, C24, A24plus);
    sike_fp2div2(C24, C24);
    j_inv(A24plus, C24, jinv);
    sike_fp2_encode(jinv, ssA);
}

// Bob's ephemeral shared secret computation
// It produces a shared secret key ssB using his secret key skB and Alice's public key pkA
// Inputs: Bob's skB is an integer in the range [0, 2^Floor(Log(2,3^159)) - 1], stored in 32 bytes.
//         Alice's pkA consists of 3 GF(p503^2) elements encoded in 378 bytes.
// Output: a shared secret ssB that consists of one element in GF(p503^2) encoded in 126 bytes.
static void ex_iso_B(const uint8_t* skB, const uint8_t* pkA, uint8_t* ssB)
{
    point_proj_t R, pts[MAX_INT_POINTS_BOB];
    f2elm_t coeff[3], PKB[3], jinv;
    f2elm_t A24plus = F2ELM_INIT;
    f2elm_t A24minus = F2ELM_INIT;
    f2elm_t A = F2ELM_INIT;
    unsigned int m, index = 0, pts_index[MAX_INT_POINTS_BOB], npts = 0, ii = 0;

    // Initialize images of Alice's basis
    fp2_decode(pkA, PKB[0]);
    fp2_decode(pkA + SIDHp503_JINV_BYTESZ, PKB[1]);
    fp2_decode(pkA + 2*SIDHp503_JINV_BYTESZ, PKB[2]);

    // Initialize constants
    get_A(PKB[0], PKB[1], PKB[2], A);
    sike_fpadd(p503.mont_one, p503.mont_one, A24minus->c0);
    sike_fp2add(A, A24minus, A24plus);
    sike_fp2sub(A, A24minus, A24minus);

    // Retrieve kernel point
    LADDER3PT(PKB[0], PKB[1], PKB[2], skB, 0, R, A);

    // Traverse tree
    index = 0;
    for (size_t row = 1; row < B_max; row++) {
        while (index < B_max-row) {
            sike_fp2copy(R->X, pts[npts]->X);
            sike_fp2copy(R->Z, pts[npts]->Z);
            pts_index[npts++] = index;
            m = p503.B_strat[ii++];
            xTPLe(R, R, A24minus, A24plus, m);
            index += m;
        }
        get_3_isog(R, A24minus, A24plus, coeff);

        for (size_t i = 0; i < npts; i++) {
            eval_3_isog(pts[i], coeff);
        }

        sike_fp2copy(pts[npts-1]->X, R->X);
        sike_fp2copy(pts[npts-1]->Z, R->Z);
        index = pts_index[npts-1];
        npts -= 1;
    }

    get_3_isog(R, A24minus, A24plus, coeff);
    sike_fp2add(A24plus, A24minus, A);
    sike_fp2add(A, A, A);
    sike_fp2sub(A24plus, A24minus, A24plus);
    j_inv(A, A24plus, jinv);
    sike_fp2_encode(jinv, ssB);
}

int SIKE_keypair(uint8_t out_priv[SIKEp503_PRV_BYTESZ],
                 uint8_t out_pub[SIKEp503_PUB_BYTESZ]) {
  int ret = 0;

  // Calculate private key for Alice. Needs to be in range [0, 2^0xFA - 1] and <
  // 253 bits
  BIGNUM *bn_sidh_prv = BN_new();
  if (!bn_sidh_prv ||
      !BN_rand(bn_sidh_prv, SIDHp503_PRV_B_BITSZ, BN_RAND_TOP_ONE,
               BN_RAND_BOTTOM_ANY) ||
      !BN_bn2le_padded(out_priv, BITS_TO_BYTES(SIDHp503_PRV_B_BITSZ),
                       bn_sidh_prv)) {
    goto end;
  }

  gen_iso_B(out_priv, out_pub);
  ret = 1;

end:
  BN_free(bn_sidh_prv);
  return ret;
}

void SIKE_encaps(uint8_t out_shared_key[SIKEp503_SS_BYTESZ],
                 uint8_t out_ciphertext[SIKEp503_CT_BYTESZ],
                 const uint8_t pub_key[SIKEp503_PUB_BYTESZ]) {
  // Secret buffer is reused by the function to store some ephemeral
  // secret data. It's size must be maximum of SHA256_CBLOCK,
  // SIKEp503_MSG_BYTESZ and SIDHp503_PRV_A_BITSZ in bytes.
  uint8_t secret[SHA256_CBLOCK];
  uint8_t j[SIDHp503_JINV_BYTESZ];
  uint8_t temp[SIKEp503_MSG_BYTESZ + SIKEp503_CT_BYTESZ];
  SHA256_CTX ctx;

  // Generate secret key for A
  // secret key A = HMAC({0,1}^n || pub_key), G) mod SIDHp503_PRV_A_BITSZ
  RAND_bytes(temp, SIKEp503_MSG_BYTESZ);

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, temp, SIKEp503_MSG_BYTESZ);
  SHA256_Update(&ctx, pub_key, SIKEp503_PUB_BYTESZ);
  SHA256_Final(secret, &ctx);
  hmac_sum(secret, BITS_TO_BYTES(SIDHp503_PRV_A_BITSZ), G, secret);
  secret[BITS_TO_BYTES(SIDHp503_PRV_A_BITSZ) - 1] &=
      (1 << (SIDHp503_PRV_A_BITSZ % 8)) - 1;

  // Generate public key for A - first part of the ciphertext
  gen_iso_A(secret, out_ciphertext);

  // Generate c1:
  //  h = HMAC(j-invariant(secret key A, public key B), F)
  // c1 = h ^ m
  ex_iso_A(secret, pub_key, j);
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, j, sizeof(j));
  SHA256_Final(secret, &ctx);
  hmac_sum(secret, SIKEp503_MSG_BYTESZ, F, secret);

  // c1 = h ^ m
  uint8_t *c1 = &out_ciphertext[SIKEp503_PUB_BYTESZ];
  for (size_t i = 0; i < SIKEp503_MSG_BYTESZ; i++) {
    c1[i] = temp[i] ^ secret[i];
  }

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, temp, SIKEp503_MSG_BYTESZ);
  SHA256_Update(&ctx, out_ciphertext, SIKEp503_CT_BYTESZ);
  SHA256_Final(secret, &ctx);
  // Generate shared secret out_shared_key = HMAC(m||out_ciphertext, F)
  hmac_sum(out_shared_key, SIKEp503_SS_BYTESZ, H, secret);
}

void SIKE_decaps(uint8_t out_shared_key[SIKEp503_SS_BYTESZ],
                 const uint8_t ciphertext[SIKEp503_CT_BYTESZ],
                 const uint8_t pub_key[SIKEp503_PUB_BYTESZ],
                 const uint8_t priv_key[SIKEp503_PRV_BYTESZ]) {
  // Secret buffer is reused by the function to store some ephemeral
  // secret data. It's size must be maximum of SHA256_CBLOCK,
  // SIKEp503_MSG_BYTESZ and SIDHp503_PRV_A_BITSZ in bytes.
  uint8_t secret[SHA256_CBLOCK];
  uint8_t j[SIDHp503_JINV_BYTESZ];
  uint8_t c0[SIKEp503_PUB_BYTESZ];
  uint8_t temp[SIKEp503_MSG_BYTESZ];
  uint8_t shared_nok[SIKEp503_MSG_BYTESZ];
  SHA256_CTX ctx;

  RAND_bytes(shared_nok, SIKEp503_MSG_BYTESZ);

  // Recover m
  // Let ciphertext = c0 || c1 - both have fixed sizes
  // m = F(j-invariant(c0, priv_key)) ^ c1
  ex_iso_B(priv_key, ciphertext, j);

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, j, sizeof(j));
  SHA256_Final(secret, &ctx);
  hmac_sum(secret, SIKEp503_MSG_BYTESZ, F, secret);

  const uint8_t *c1 = &ciphertext[sizeof(c0)];
  for (size_t i = 0; i < SIKEp503_MSG_BYTESZ; i++) {
    temp[i] = c1[i] ^ secret[i];
  }

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, temp, SIKEp503_MSG_BYTESZ);
  SHA256_Update(&ctx, pub_key, SIKEp503_PUB_BYTESZ);
  SHA256_Final(secret, &ctx);
  hmac_sum(secret, BITS_TO_BYTES(SIDHp503_PRV_A_BITSZ), G, secret);

  // Recover secret key A = G(m||pub_key) mod
  secret[BITS_TO_BYTES(SIDHp503_PRV_A_BITSZ) - 1] &=
      (1 << (SIDHp503_PRV_A_BITSZ % 8)) - 1;

  // Recover c0 = public key A
  gen_iso_A(secret, c0);
  crypto_word_t ok = constant_time_is_zero_w(
      CRYPTO_memcmp(c0, ciphertext, SIKEp503_PUB_BYTESZ));
  for (size_t i = 0; i < SIKEp503_MSG_BYTESZ; i++) {
    temp[i] = constant_time_select_8(ok, temp[i], shared_nok[i]);
  }

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, temp, SIKEp503_MSG_BYTESZ);
  SHA256_Update(&ctx, ciphertext, SIKEp503_CT_BYTESZ);
  SHA256_Final(secret, &ctx);
  hmac_sum(out_shared_key, SIKEp503_SS_BYTESZ, H, secret);
}
