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

#![deny(
    missing_docs,
    clippy::indexing_slicing,
    clippy::unwrap_used,
    clippy::panic,
    clippy::expect_used
)]

//! Rust boringssl binding

extern crate core;
use core::ops::Not;

/// BoringSSL implemented plain aes operations.
pub mod aes;

/// BoringSSL implemented hmac operations.
pub mod hmac;

/// BoringSSL implemented hash functions.
pub mod digest;

/// Used for handling result types from C APIs.
trait PanicResultHandler {
    /// Panics if a C api returns an invalid result
    /// Used for APIs which return error codes for allocation failures.
    fn panic_if_error(&self);
}

impl PanicResultHandler for i32 {
    /// BoringSSL APIs return 1 on success or 0 on allocation failure.
    #[allow(clippy::expect_used)]
    fn panic_if_error(&self) {
        self.gt(&0).then_some(()).expect("allocation failed!")
    }
}

impl<T> PanicResultHandler for *mut T {
    /// Boringssl APIs return NULL on allocation failure for APIs that return a CTX.
    #[allow(clippy::expect_used)]
    fn panic_if_error(&self) {
        self.is_null()
            .not()
            .then_some(())
            .expect("allocation failed!")
    }
}

struct CSlice<'a>(&'a [u8]);

impl CSlice<'_> {
    pub fn as_ptr<T>(&self) -> *const T {
        if self.0.is_empty() {
            std::ptr::null()
        } else {
            self.0.as_ptr() as *const T
        }
    }
}

impl<'a> From<&'a [u8]> for CSlice<'a> {
    fn from(value: &'a [u8]) -> Self {
        Self(value)
    }
}

/// A helper trait implemented by types which reference borrowed foreign types.
///
/// # Safety
///
/// Implementations of `ForeignTypeRef` must guarantee the following:
///
/// - `Self::from_ptr(x).as_ptr() == x`
/// - `Self::from_mut_ptr(x).as_ptr() == x`
unsafe trait ForeignTypeRef: Sized {
    /// The raw C type.
    type CType;

    /// Constructs a shared instance of this type from its raw type.
    ///
    /// # Safety
    ///
    /// `ptr` must be a valid, immutable, instance of the type for the `'a` lifetime.
    #[inline]
    unsafe fn from_ptr<'a>(ptr: *mut Self::CType) -> &'a Self {
        debug_assert!(!ptr.is_null());
        &*(ptr as *mut _)
    }

    /// Constructs a mutable reference of this type from its raw type.
    ///
    /// # Safety
    ///
    /// `ptr` must be a valid, unique, instance of the type for the `'a` lifetime.
    #[inline]
    unsafe fn from_ptr_mut<'a>(ptr: *mut Self::CType) -> &'a mut Self {
        debug_assert!(!ptr.is_null());
        &mut *(ptr as *mut _)
    }

    /// Returns a raw pointer to the wrapped value.
    #[inline]
    fn as_ptr(&self) -> *mut Self::CType {
        self as *const _ as *mut _
    }
}
