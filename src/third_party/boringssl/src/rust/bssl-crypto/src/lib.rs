// Copyright 2023 The BoringSSL Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#![deny(
    missing_docs,
    unsafe_op_in_unsafe_fn,
    clippy::indexing_slicing,
    clippy::unwrap_used,
    clippy::panic,
    clippy::expect_used
)]
#![cfg_attr(not(any(feature = "std", test)), no_std)]

//! Rust BoringSSL bindings

extern crate alloc;
extern crate core;

#[cfg(feature = "mlalgs")]
use alloc::boxed::Box;

use alloc::vec::Vec;
use core::ffi::c_void;

#[macro_use]
mod macros;

pub mod aead;
pub mod aes;

/// Ciphers.
pub mod cipher;

pub mod digest;
pub mod ec;
pub mod ecdh;
pub mod ecdsa;
pub mod ed25519;
pub mod hkdf;
pub mod hmac;
pub mod hpke;
#[cfg(feature = "mlalgs")]
pub mod mldsa;
#[cfg(feature = "mlalgs")]
pub mod mlkem;
pub mod rsa;
pub mod slhdsa;
pub mod x25519;

mod scoped;

#[cfg(test)]
mod test_helpers;

mod mem;
pub use mem::constant_time_compare;

mod rand;
pub use rand::{rand_array, rand_bytes};

/// Error type for when a "signature" (either a public-key signature or a MAC)
/// is incorrect.
#[derive(Debug)]
pub struct InvalidSignatureError;

/// FfiSlice exists to provide `as_ffi_ptr` on slices. Calling `as_ptr` on an
/// empty Rust slice may return the alignment of the type, rather than NULL, as
/// the pointer. When passing pointers into C/C++ code, that is not a valid
/// pointer. Thus this method should be used whenever passing a pointer to a
/// slice into BoringSSL code.
trait FfiSlice {
    fn as_ffi_ptr(&self) -> *const u8;
    fn as_ffi_void_ptr(&self) -> *const c_void {
        self.as_ffi_ptr() as *const c_void
    }
}

impl FfiSlice for [u8] {
    fn as_ffi_ptr(&self) -> *const u8 {
        if self.is_empty() {
            core::ptr::null()
        } else {
            self.as_ptr()
        }
    }
}

impl<const N: usize> FfiSlice for [u8; N] {
    fn as_ffi_ptr(&self) -> *const u8 {
        if N == 0 {
            core::ptr::null()
        } else {
            self.as_ptr()
        }
    }
}

/// See the comment [`FfiSlice`].
trait FfiMutSlice {
    fn as_mut_ffi_ptr(&mut self) -> *mut u8;
}

impl FfiMutSlice for [u8] {
    fn as_mut_ffi_ptr(&mut self) -> *mut u8 {
        if self.is_empty() {
            core::ptr::null_mut()
        } else {
            self.as_mut_ptr()
        }
    }
}

impl<const N: usize> FfiMutSlice for [u8; N] {
    fn as_mut_ffi_ptr(&mut self) -> *mut u8 {
        if N == 0 {
            core::ptr::null_mut()
        } else {
            self.as_mut_ptr()
        }
    }
}

/// This is a helper struct which provides functions for passing slices over FFI.
///
/// Deprecated: use `FfiSlice` which adds less noise and lets one grep for `as_ptr`
/// as a sign of something to check.
struct CSlice<'a>(&'a [u8]);

impl<'a> From<&'a [u8]> for CSlice<'a> {
    fn from(value: &'a [u8]) -> Self {
        Self(value)
    }
}

impl CSlice<'_> {
    /// Returns a raw pointer to the value, which is safe to pass over FFI.
    pub fn as_ptr<T>(&self) -> *const T {
        if self.0.is_empty() {
            core::ptr::null()
        } else {
            self.0.as_ptr() as *const T
        }
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }
}

/// This is a helper struct which provides functions for passing mutable slices over FFI.
///
/// Deprecated: use `FfiMutSlice` which adds less noise and lets one grep for
/// `as_ptr` as a sign of something to check.
struct CSliceMut<'a>(&'a mut [u8]);

impl CSliceMut<'_> {
    /// Returns a raw pointer to the value, which is safe to pass over FFI.
    pub fn as_mut_ptr<T>(&mut self) -> *mut T {
        if self.0.is_empty() {
            core::ptr::null_mut()
        } else {
            self.0.as_mut_ptr() as *mut T
        }
    }

    pub fn len(&self) -> usize {
        self.0.len()
    }
}

impl<'a> From<&'a mut [u8]> for CSliceMut<'a> {
    fn from(value: &'a mut [u8]) -> Self {
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
/// - `Self::from_ptr_mut(x).as_ptr() == x`
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
        unsafe { &*(ptr as *mut _) }
    }

    /// Returns a raw pointer to the wrapped value.
    #[inline]
    fn as_ptr(&self) -> *mut Self::CType {
        self as *const _ as *mut _
    }
}

/// Returns a BoringSSL structure that is initialized by some function.
/// Requires that the given function completely initializes the value.
///
/// (Tagged `unsafe` because a no-op argument would otherwise expose
/// uninitialized memory.)
unsafe fn initialized_struct<T, F>(init: F) -> T
where
    F: FnOnce(*mut T),
{
    let mut out_uninit = core::mem::MaybeUninit::<T>::uninit();
    init(out_uninit.as_mut_ptr());
    unsafe { out_uninit.assume_init() }
}

/// Returns a BoringSSL structure that is initialized by some function.
/// Requires that the given function completely initializes the value or else
/// returns false.
///
/// (Tagged `unsafe` because a no-op argument would otherwise expose
/// uninitialized memory.)
unsafe fn initialized_struct_fallible<T, F>(init: F) -> Option<T>
where
    F: FnOnce(*mut T) -> bool,
{
    let mut out_uninit = core::mem::MaybeUninit::<T>::uninit();
    if init(out_uninit.as_mut_ptr()) {
        Some(unsafe { out_uninit.assume_init() })
    } else {
        None
    }
}

/// Returns a boxed BoringSSL structure that is initialized by some function.
/// Requires that the given function completely initializes the value.
///
/// Safety: the argument must fully initialize the pointed-to `T`.
#[cfg(feature = "mlalgs")]
unsafe fn initialized_boxed_struct<T, F>(init: F) -> Box<T>
where
    F: FnOnce(*mut T),
{
    let mut out_uninit = Box::new(core::mem::MaybeUninit::<T>::uninit());
    init(out_uninit.as_mut_ptr());
    unsafe { out_uninit.assume_init() }
}

/// Returns a boxed BoringSSL structure that is initialized by some function.
/// Requires that the given function completely initializes the value or else
/// returns false.
///
/// Safety: the argument must fully initialize the pointed-to `T` if it returns
/// true. If it returns false then there are no safety requirements.
#[cfg(feature = "mlalgs")]
unsafe fn initialized_boxed_struct_fallible<T, F>(init: F) -> Option<Box<T>>
where
    F: FnOnce(*mut T) -> bool,
{
    let mut out_uninit = Box::new(core::mem::MaybeUninit::<T>::uninit());
    if init(out_uninit.as_mut_ptr()) {
        Some(unsafe { out_uninit.assume_init() })
    } else {
        None
    }
}

/// Wrap a closure that initializes an output buffer and return that buffer as
/// an array. Requires that the closure fully initialize the given buffer.
///
/// Safety: the closure must fully initialize the array.
unsafe fn with_output_array<const N: usize, F>(func: F) -> [u8; N]
where
    F: FnOnce(*mut u8, usize),
{
    let mut out_uninit = core::mem::MaybeUninit::<[u8; N]>::uninit();
    let out_ptr = if N != 0 {
        out_uninit.as_mut_ptr() as *mut u8
    } else {
        core::ptr::null_mut()
    };
    func(out_ptr, N);
    // Safety: `func` promises to fill all of `out_uninit`.
    unsafe { out_uninit.assume_init() }
}

/// Wrap a closure that initializes an output buffer and return that buffer as
/// an array. The closure returns a [`core::ffi::c_int`] and, if the return value
/// is not one, then the initialization is assumed to have failed and [None] is
/// returned. Otherwise, this function requires that the closure fully
/// initialize the given buffer.
///
/// Safety: the closure must fully initialize the array if it returns one.
unsafe fn with_output_array_fallible<const N: usize, F>(func: F) -> Option<[u8; N]>
where
    F: FnOnce(*mut u8, usize) -> bool,
{
    let mut out_uninit = core::mem::MaybeUninit::<[u8; N]>::uninit();
    let out_ptr = if N != 0 {
        out_uninit.as_mut_ptr() as *mut u8
    } else {
        core::ptr::null_mut()
    };
    if func(out_ptr, N) {
        // Safety: `func` promises to fill all of `out_uninit` if it returns one.
        unsafe { Some(out_uninit.assume_init()) }
    } else {
        None
    }
}

/// Wrap a closure that writes at most `max_output` bytes to fill a vector.
/// It must return the number of bytes written.
///
/// Safety: `F` must not write more than `max_output` bytes and must return
/// the number of bytes written.
#[allow(clippy::unwrap_used)]
unsafe fn with_output_vec<F>(max_output: usize, func: F) -> Vec<u8>
where
    F: FnOnce(*mut u8) -> usize,
{
    unsafe {
        with_output_vec_fallible(max_output, |out_buf| Some(func(out_buf)))
            // The closure cannot fail and thus neither can
            // `with_output_array_fallible`.
            .unwrap()
    }
}

/// Wrap a closure that writes at most `max_output` bytes to fill a vector.
/// If successful, it must return the number of bytes written.
///
/// Safety: `F` must not write more than `max_output` bytes and must return
/// the number of bytes written or else return `None` to indicate failure.
unsafe fn with_output_vec_fallible<F>(max_output: usize, func: F) -> Option<Vec<u8>>
where
    F: FnOnce(*mut u8) -> Option<usize>,
{
    let mut ret = Vec::with_capacity(max_output);
    let out = ret.spare_capacity_mut();
    let out_buf = out
        .get_mut(0)
        .map_or(core::ptr::null_mut(), |x| x.as_mut_ptr());

    let num_written = func(out_buf)?;
    assert!(num_written <= ret.capacity());

    unsafe {
        // Safety: `num_written` bytes have been written to.
        ret.set_len(num_written);
    }

    Some(ret)
}

/// Buffer represents an owned chunk of memory on the BoringSSL heap.
/// Call `as_ref()` to get a `&[u8]` from it.
pub struct Buffer {
    // This pointer is always allocated by BoringSSL and must be freed using
    // `OPENSSL_free`.
    ptr: *mut u8,
    len: usize,
}

impl Buffer {
    /// Safety: `ptr` must point to `len` bytes, allocated by BoringSSL.
    unsafe fn new(ptr: *mut u8, len: usize) -> Buffer {
        Buffer { ptr, len }
    }
}

impl AsRef<[u8]> for Buffer {
    fn as_ref(&self) -> &[u8] {
        if self.len == 0 {
            return &[];
        }
        // Safety: `ptr` and `len` describe a valid area of memory and `ptr`
        // must be Rust-valid because `len` is non-zero.
        unsafe { core::slice::from_raw_parts(self.ptr, self.len) }
    }
}

impl Drop for Buffer {
    fn drop(&mut self) {
        // Safety: `ptr` is owned by this object and is on the BoringSSL heap.
        unsafe {
            bssl_sys::OPENSSL_free(self.ptr as *mut core::ffi::c_void);
        }
    }
}

#[cfg(feature = "mlalgs")]
fn as_cbs(buf: &[u8]) -> bssl_sys::CBS {
    bssl_sys::CBS {
        data: buf.as_ffi_ptr(),
        len: buf.len(),
    }
}

/// Calls `parse_func` with a `CBS` structure pointing at `data`.
/// If that returns a null pointer then it returns [None].
/// Otherwise, if there's still data left in CBS, it calls `free_func` on the
/// pointer and returns [None]. Otherwise it returns the pointer.
fn parse_with_cbs<T, Parse, Free>(data: &[u8], free_func: Free, parse_func: Parse) -> Option<*mut T>
where
    Parse: FnOnce(*mut bssl_sys::CBS) -> *mut T,
    Free: FnOnce(*mut T),
{
    // Safety: type checking ensures that `cbs` is the correct size.
    let mut cbs =
        unsafe { initialized_struct(|cbs| bssl_sys::CBS_init(cbs, data.as_ffi_ptr(), data.len())) };
    let ptr = parse_func(&mut cbs);
    if ptr.is_null() {
        return None;
    }
    // Safety: `cbs` is still valid after parsing.
    if unsafe { bssl_sys::CBS_len(&cbs) } != 0 {
        // Safety: `ptr` is still owned by this function.
        free_func(ptr);
        return None;
    }
    Some(ptr)
}

/// Calls `func` with a `CBB` pointer and returns a [Buffer] of the ultimate
/// contents of that CBB.
#[allow(clippy::unwrap_used)]
fn cbb_to_buffer<F: FnOnce(*mut bssl_sys::CBB)>(initial_capacity: usize, func: F) -> Buffer {
    // Safety: type checking ensures that `cbb` is the correct size.
    let mut cbb = unsafe {
        initialized_struct_fallible(|cbb| bssl_sys::CBB_init(cbb, initial_capacity) == 1)
    }
    // `CBB_init` only fails if out of memory, which isn't something that this crate handles.
    .unwrap();
    func(&mut cbb);

    let mut ptr: *mut u8 = core::ptr::null_mut();
    let mut len: usize = 0;
    // `CBB_finish` only fails on programming error, which we convert into a
    // panic.
    assert_eq!(1, unsafe {
        bssl_sys::CBB_finish(&mut cbb, &mut ptr, &mut len)
    });

    // Safety: `ptr` is on the BoringSSL heap and ownership is returned by
    // `CBB_finish`.
    unsafe { Buffer::new(ptr, len) }
}

#[cfg(feature = "mlalgs")]
/// Calls `func` with a `CBB` pointer that has been initialized to a vector
/// of `len` bytes. That function must write exactly `len` bytes to the
/// `CBB`. Those bytes are then returned as a vector.
#[allow(clippy::unwrap_used)]
fn cbb_to_vec<F: FnOnce(*mut bssl_sys::CBB)>(len: usize, func: F) -> Vec<u8> {
    let mut boxed = Box::new_uninit_slice(len);
    // Safety: type checking ensures that `cbb` is the correct size.
    let mut cbb = unsafe {
        initialized_struct_fallible(|cbb| {
            bssl_sys::CBB_init_fixed(cbb, boxed.as_mut_ptr() as *mut u8, len) == 1
        })
    }
    // `CBB_init_fixed` never fails and does not allocate.
    .unwrap();

    func(&mut cbb);

    unsafe {
        assert_eq!(bssl_sys::CBB_len(&cbb), len);
        // `boxed` has been fully written, as checked on the previous line.
        boxed.assume_init().into()
    }
}

/// Used to prevent external implementations of internal traits.
mod sealed {
    pub struct Sealed;
}
