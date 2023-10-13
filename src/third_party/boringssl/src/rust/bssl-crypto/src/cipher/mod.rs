/* Copyright (c) 2023, Google Inc.
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
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

use crate::{CSlice, CSliceMut};
use bssl_sys::EVP_CIPHER;
use std::ffi::c_int;
use std::marker::PhantomData;

/// AES-CTR stream cipher operations.
pub mod aes_ctr;

/// Error returned in the event of an unsuccessful cipher operation.
#[derive(Debug)]
pub struct CipherError;

/// Synchronous stream cipher trait.
pub trait StreamCipher {
    /// The byte array key type which specifies the size of the key used to instantiate the cipher.
    type Key: AsRef<[u8]>;

    /// The byte array nonce type which specifies the size of the nonce used in the cipher
    /// operations.
    type Nonce: AsRef<[u8]>;

    /// Instantiate a new instance of a stream cipher from a `key` and `iv`.
    fn new(key: &Self::Key, iv: &Self::Nonce) -> Self;

    /// Applies the cipher keystream to `buffer` in place, returning CipherError on an unsuccessful
    /// operation.
    fn apply_keystream(&mut self, buffer: &mut [u8]) -> Result<(), CipherError>;
}

trait EvpCipherType {
    type Key: AsRef<[u8]>;
    type Nonce: AsRef<[u8]>;
    fn evp_cipher() -> *const EVP_CIPHER;
}

struct EvpAes128Ctr;
impl EvpCipherType for EvpAes128Ctr {
    type Key = [u8; 16];
    type Nonce = [u8; 16];
    fn evp_cipher() -> *const EVP_CIPHER {
        // Safety:
        // - this just returns a constant value
        unsafe { bssl_sys::EVP_aes_128_ctr() }
    }
}

struct EvpAes256Ctr;
impl EvpCipherType for EvpAes256Ctr {
    type Key = [u8; 32];
    type Nonce = [u8; 16];
    fn evp_cipher() -> *const EVP_CIPHER {
        // Safety:
        // - this just returns a constant value
        unsafe { bssl_sys::EVP_aes_256_ctr() }
    }
}

// Internal cipher implementation which wraps EVP_CIPHER_*, where K is the size of the Key and I is
// the size of the IV. This must only be exposed publicly by types who ensure that K is the correct
// size for the given CipherType. This can be checked via bssl_sys::EVP_CIPHER_key_length.
//
// WARNING: This is not safe to re-use for the CBC mode of operation since it is applying the
// key stream in-place.
struct Cipher<C: EvpCipherType> {
    ctx: *mut bssl_sys::EVP_CIPHER_CTX,
    _marker: PhantomData<C>,
}

impl<C: EvpCipherType> Cipher<C> {
    fn new(key: &C::Key, iv: &C::Nonce) -> Self {
        // Safety:
        // - Panics on allocation failure.
        let ctx = unsafe { bssl_sys::EVP_CIPHER_CTX_new() };
        assert!(!ctx.is_null());

        let key_cslice = CSlice::from(key.as_ref());
        let iv_cslice = CSlice::from(iv.as_ref());

        // Safety:
        // - Key size and iv size must be properly set by the higher level wrapper types.
        // - Panics on allocation failure.
        let result = unsafe {
            bssl_sys::EVP_EncryptInit_ex(
                ctx,
                C::evp_cipher(),
                std::ptr::null_mut(),
                key_cslice.as_ptr(),
                iv_cslice.as_ptr(),
            )
        };
        assert_eq!(result, 1);

        Self {
            ctx,
            _marker: Default::default(),
        }
    }

    fn apply_keystream_in_place(&mut self, buffer: &mut [u8]) -> Result<(), CipherError> {
        let mut cslice_buf_mut = CSliceMut::from(buffer);
        let mut out_len = 0;

        let buff_len_int = c_int::try_from(cslice_buf_mut.len()).map_err(|_| CipherError)?;

        // Safety:
        // - The output buffer provided is always large enough for an in-place operation.
        let result = unsafe {
            bssl_sys::EVP_EncryptUpdate(
                self.ctx,
                cslice_buf_mut.as_mut_ptr(),
                &mut out_len,
                cslice_buf_mut.as_mut_ptr(),
                buff_len_int,
            )
        };
        if result == 1 {
            assert_eq!(out_len as usize, cslice_buf_mut.len());
            Ok(())
        } else {
            Err(CipherError)
        }
    }
}

impl<C: EvpCipherType> Drop for Cipher<C> {
    fn drop(&mut self) {
        // Safety:
        // - `self.ctx` was allocated by `EVP_CIPHER_CTX_new` and has not yet been freed.
        unsafe { bssl_sys::EVP_CIPHER_CTX_free(self.ctx) }
    }
}
