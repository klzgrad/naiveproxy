// Generated from Bedrock code in Fiat Cryptography. Avoid editing directly.

static_assert(BR_WORD_MAX == UINT64_MAX, "");

static inline void p256_coord_sub(br_word_t out, br_word_t x, br_word_t y) {
  br_word_t borrow, t0, t1, t2, t3, mask, carry, r0, r1, r2, r3;
  borrow = br_full_sub(_br_load(x), _br_load(y), (br_word_t)0, &t0);
  borrow = br_full_sub(_br_load(x+8), _br_load(y+8), borrow, &t1);
  borrow = br_full_sub(_br_load((x+8)+8), _br_load((y+8)+8), borrow, &t2);
  borrow = br_full_sub(_br_load(((x+8)+8)+8), _br_load(((y+8)+8)+8), borrow, &t3);
  mask = br_value_barrier(0u-borrow);
  carry = br_full_add(t0, mask, (br_word_t)0, &r0);
  carry = br_full_add(t1, mask&0xffffffff, carry, &r1);
  carry = br_full_add(t2, (br_word_t)0, carry, &r2);
  carry = br_full_add(t3, mask&0xffffffff00000001, carry, &r3);
  _br_store(out, r0);
  _br_store(out+8, r1);
  _br_store((out+8)+8, r2);
  _br_store(((out+8)+8)+8, r3);
  return;
}
