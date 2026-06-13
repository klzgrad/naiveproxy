// Copyright 2026 The BoringSSL Authors
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

//! TLS credentials

use alloc::{
    boxed::Box,
    vec,
    vec::Vec, //
};
use core::{
    ffi::{
        CStr,
        c_int, //
    },
    fmt::Debug,
    marker::PhantomData,
    mem::forget,
    ptr::{
        NonNull,
        null_mut, //
    }, //
};

use bssl_x509::{
    errors::PemReason,
    keys::PrivateKey, //
};

use crate::{
    check_lib_error,
    config::ConfigurationError,
    context::CertificateCache,
    crypto_buffer_wrapper,
    errors::{
        Error,
        IoError, //
    },
    ffi::{
        Alloc,
        Bio,
        sanitize_slice,
        slice_into_ffi_raw_parts, //
    },
    has_duplicates, //
};

pub(crate) mod methods;

/// TLS credentials builder
pub struct TlsCredentialBuilder<Mode>(NonNull<bssl_sys::SSL_CREDENTIAL>, PhantomData<fn() -> Mode>);

/// X.509 credential
pub enum X509Mode {}

pub(crate) trait NeedsPrivateKey {}

impl NeedsPrivateKey for X509Mode {}

// Safety: At this type state, the credential handle is exclusively owned.
unsafe impl<M> Send for TlsCredentialBuilder<M> {}

impl<M> Drop for TlsCredentialBuilder<M> {
    fn drop(&mut self) {
        unsafe {
            // Safety: `self.0` is still valid at dropping.
            bssl_sys::SSL_CREDENTIAL_free(self.0.as_ptr());
        }
    }
}

impl<M> TlsCredentialBuilder<M> {
    fn ptr(&mut self) -> *mut bssl_sys::SSL_CREDENTIAL {
        self.0.as_ptr()
    }

    fn set_ex_data(mut self) -> Self {
        let rc = unsafe {
            // Safety:
            // - this method is called exactly once during construction.
            // - the ex_data index will be generated correctly and exactly once.
            // - the `SSL_CREDENTIAL*` handle is already valid, witnessed by `self`.
            bssl_sys::SSL_CREDENTIAL_set_ex_data(
                self.ptr(),
                *methods::TLS_CREDENTIAL_METHOD,
                Box::into_raw(Box::new(methods::RustCredentialMethods::default())) as _,
            )
        };
        assert_eq!(rc, 1);
        self
    }
}

impl TlsCredentialBuilder<X509Mode> {
    /// Construct X.509-powered credential instance.
    pub fn new() -> Self {
        let this = Self(
            NonNull::new(unsafe {
                // Safety: this call has no side-effect other than allocation.
                bssl_sys::SSL_CREDENTIAL_new_x509()
            })
            .expect("allocation failure"),
            PhantomData,
        );
        this.set_ex_data()
    }
}

impl<M> TlsCredentialBuilder<M>
where
    M: NeedsPrivateKey,
{
    /// Set [`SignatureAlgorithm`] preferences.
    ///
    /// This controls which signature algorithms will be used with this credential.
    pub fn with_signing_algorithm_preferences(
        &mut self,
        algs: &[SignatureAlgorithm],
    ) -> Result<&mut Self, Error> {
        let algs: &[u16] = unsafe {
            // Safety: `SignatureAlgorithm` has a `repr(u16)`
            core::mem::transmute(algs)
        };
        if has_duplicates(algs) {
            return Err(Error::Configuration(
                ConfigurationError::DuplicatedParameters,
            ));
        }
        let (ptr, len) = slice_into_ffi_raw_parts(algs);
        check_lib_error!(unsafe {
            // Safety
            bssl_sys::SSL_CREDENTIAL_set1_signing_algorithm_prefs(self.ptr(), ptr, len)
        });
        Ok(self)
    }

    /// Set a private key.
    ///
    /// **NOTE**: Call this method after setting the certificates with
    /// [`Self::with_certificate_chain`].
    ///
    /// This method errs when the private key does not match the public key in the leaf certificate
    /// as configured through [`Self::with_certificate_chain`].
    pub fn with_private_key(&mut self, mut key: PrivateKey) -> Result<&mut Self, Error> {
        let rc = unsafe {
            // Safety:
            // - both `key.0` and `self.0` are still valid.
            // - `bssl-tls` uses the same BoringSSL as that is linked to `bssl-sys` and `bssl-x509`.
            // - the `EVP_PKEY` handle will outlive `key` because this call will claim ownership.
            bssl_sys::SSL_CREDENTIAL_set1_private_key(self.ptr(), key.as_mut_ptr())
        };
        if rc != 1 {
            Err(Error::Configuration(ConfigurationError::MismatchingKeyPair))
        } else {
            Ok(self)
        }
    }
}

impl TlsCredentialBuilder<X509Mode> {
    /// Set certificate chain.
    ///
    /// The leaf, also known as end-entity, certificate **must come first** in `certs`.
    /// This method returns [`ConfigurationError::InvalidParameters`] if `certs` are empty.
    pub fn with_certificate_chain(&mut self, certs: &[Certificate]) -> Result<&mut Self, Error> {
        if certs.is_empty() {
            return Err(Error::Configuration(ConfigurationError::InvalidParameters));
        }
        let certs: &[*mut bssl_sys::CRYPTO_BUFFER] = unsafe {
            // Safety: `Certificate` is a `repr(transparent)` wrapper around
            // `*mut bssl_sys::CRYPTO_BUFFER`.
            core::mem::transmute(certs)
        };
        let (ptr, len) = slice_into_ffi_raw_parts(certs);
        check_lib_error!(unsafe {
            // Safety
            bssl_sys::SSL_CREDENTIAL_set1_cert_chain(self.ptr(), ptr, len)
        });
        Ok(self)
    }

    /// Set Online Certificate Status Protocol Response.
    pub fn with_ocsp_response(&mut self, ocsp: &OcspResponse) -> Result<&mut Self, Error> {
        check_lib_error!(unsafe {
            // Safety: both `self.0` and `ocsp.0` are still live witnessed by the wrapper types.
            bssl_sys::SSL_CREDENTIAL_set1_ocsp_response(self.ptr(), ocsp.ptr())
        });
        Ok(self)
    }

    /// Set Signed Certificate Timestamp List.
    pub fn with_scts(&mut self, scts: &SignedCertificateTimestampList) -> Result<&mut Self, Error> {
        check_lib_error!(unsafe {
            // Safety: both `self.0` and `scts.0` are still live.
            bssl_sys::SSL_CREDENTIAL_set1_signed_cert_timestamp_list(self.ptr(), scts.ptr())
        });
        Ok(self)
    }

    /// Enforce a check if the peer supports the issuer of the configured certificate chain.
    ///
    /// This setting can be used for certificate chains that may not be usable by all peers.
    /// This scenario could happen with chains with fewer cross-signs or issued from a newer CA.
    /// When in force, the credential list is tried in order, so more specific credentials that
    /// enable issuer matching should generally be ordered before less specific credentials that
    /// do not.
    pub fn must_match_issuer(&mut self, match_: bool) -> Result<&mut Self, Error> {
        unsafe {
            // Safety: `self.0` is still valid.
            bssl_sys::SSL_CREDENTIAL_set_must_match_issuer(self.ptr(), if match_ { 1 } else { 0 });
        }
        Ok(self)
    }
}

impl<M> TlsCredentialBuilder<M> {
    /// Finalise the credential.
    pub fn build(mut self) -> Option<TlsCredential> {
        if unsafe {
            // Safety: `self.0` is still valid.
            bssl_sys::SSL_CREDENTIAL_is_complete(self.ptr()) == 1
        } {
            let Self(cred, _) = self;
            forget(self);
            Some(TlsCredential(cred))
        } else {
            None
        }
    }
}

/// Supported hash algorithms for TLS 1.3 PSK
///
/// See [RFC 9258] § 5.1.
///
/// [RFC 9258]: <https://datatracker.ietf.org/doc/html/rfc9258#section-5.1>
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub enum PskHash {
    /// SHA-256
    Sha256,
    /// SHA-384
    Sha384,
}

impl PskHash {
    pub(crate) fn as_evp_md(&self) -> *const bssl_sys::EVP_MD {
        match self {
            PskHash::Sha256 => unsafe {
                // Safety: `EVP_sha256` returns a valid pointer to a static `EVP_MD`.
                bssl_sys::EVP_sha256()
            },
            PskHash::Sha384 => unsafe {
                // Safety: `EVP_sha384` returns a valid pointer to a static `EVP_MD`.
                bssl_sys::EVP_sha384()
            },
        }
    }
}
/// A completely constructed TLS credential.
pub struct TlsCredential(NonNull<bssl_sys::SSL_CREDENTIAL>);

// Safety: `TlsCredential` is locked as immutable at this type state.
unsafe impl Send for TlsCredential {}
unsafe impl Sync for TlsCredential {}

impl TlsCredential {
    pub(crate) fn ptr(&self) -> *mut bssl_sys::SSL_CREDENTIAL {
        self.0.as_ptr()
    }

    /// Create a new pre-shared key credential for TLS 1.3.
    ///
    /// See [RFC 9258](https://datatracker.ietf.org/doc/html/rfc9258) for details.
    pub fn new_pre_shared_key(
        key: &[u8],
        identity: &[u8],
        hash: PskHash,
        context: &[u8],
    ) -> Result<Self, Error> {
        let (key_ptr, key_len) = slice_into_ffi_raw_parts(key);
        let (id_ptr, id_len) = slice_into_ffi_raw_parts(identity);
        let (ctx_ptr, ctx_len) = slice_into_ffi_raw_parts(context);
        let cred = unsafe {
            // Safety:
            // - `key_ptr` and `key_len` are valid for the duration of the call.
            // - `id_ptr` and `id_len` are valid for the duration of the call.
            // - `hash.as_ptr()` returns a valid static `EVP_MD` pointer.
            // - `ctx_ptr` and `ctx_len` are valid for the duration of the call.
            // - The function returns a newly allocated `SSL_CREDENTIAL` or NULL.
            bssl_sys::SSL_CREDENTIAL_new_pre_shared_key(
                key_ptr,
                key_len,
                id_ptr,
                id_len,
                hash.as_evp_md(),
                ctx_ptr,
                ctx_len,
            )
        };
        let cred = NonNull::new(cred).ok_or_else(|| Error::extract_lib_err())?;
        Ok(TlsCredential(cred))
    }
}

impl Clone for TlsCredential {
    fn clone(&self) -> Self {
        unsafe {
            // Safety: this handle is already valid by the witness of self.
            bssl_sys::SSL_CREDENTIAL_up_ref(self.ptr());
        }
        Self(self.0)
    }
}

impl Drop for TlsCredential {
    fn drop(&mut self) {
        unsafe {
            // Safety: `self.0` is still valid at dropping.
            bssl_sys::SSL_CREDENTIAL_free(self.ptr());
        }
    }
}

crypto_buffer_wrapper! {
    /// A dumb handle to a **possibly valid** TLS certificate.
    pub struct Certificate
}

impl Certificate {
    /// Parse certificates from PEM-encoded blocks.
    ///
    /// The parsing will skip anything that is not a certificate or empty.
    /// However, this method does not verify if the content is actually a valid, correctly signed
    /// DER-encoded X.509 certificate.
    /// The method returns at the first error encountered.
    pub fn parse_all_from_pem(
        cert: &[u8],
        cache: Option<&CertificateCache>,
    ) -> Result<Vec<Self>, Error> {
        let mut bio = Bio::from_bytes(cert).unwrap();
        let mut res = vec![];
        loop {
            match Self::parse_one(&mut bio, cache) {
                Ok((cert, eos)) => {
                    res.push(cert);
                    if eos {
                        return Ok(res);
                    }
                }
                Err(Error::PemReason(PemReason::NoStartLine)) => return Ok(res),
                Err(err) => return Err(err),
            }
        }
    }

    /// Parse the first certificate from the PEM-encoded data.
    pub fn parse_one_from_pem(
        cert: &[u8],
        cache: Option<&CertificateCache>,
    ) -> Result<Self, Error> {
        let mut bio = Bio::from_bytes(cert)?;
        let (cert, _) = Self::parse_one(&mut bio, cache)?;
        Ok(cert)
    }

    fn parse_one(bio: &mut Bio, cache: Option<&CertificateCache>) -> Result<(Self, bool), Error> {
        loop {
            let mut name = null_mut();
            let mut header = null_mut();
            let mut data = null_mut();
            let mut len = 0;
            let ret = unsafe {
                // Safety:
                // - `name`, `header`, `data` and `len` are valid and aligned.
                // - `bio` is still valid.
                bssl_sys::PEM_read_bio(
                    bio.ptr(),
                    &raw mut name,
                    &raw mut header,
                    &raw mut data,
                    &raw mut len,
                )
            };
            let eof = unsafe {
                // Safety: `bio` is still valid.
                bssl_sys::BIO_eof(bio.ptr()) != 0
            };
            let name = Alloc(name);
            let header = Alloc(header);
            let data = Alloc(data);
            if name.0.is_null() || header.0.is_null() || data.0.is_null() || ret == 0 {
                return Err(Error::extract_lib_err());
            }
            if len == 0 {
                continue;
            }
            let Ok(len) = usize::try_from(len) else {
                return Err(Error::Io(IoError::TooLong));
            };
            let buf = unsafe {
                // Safety:
                // - `name.0` is NUL-terminated by BoringSSL invariant.
                // - the lifetime of `buf` is outlived by `name.0`.
                CStr::from_ptr(name.0)
            };
            if buf.to_bytes() != b"CERTIFICATE" {
                continue;
            }
            let Some(contents) = (unsafe {
                // Safety: the slice is only used within the loop and we will copy the contents
                // when constructing the certificate object.
                sanitize_slice(data.0, len)
            }) else {
                return Err(Error::Io(IoError::TooLong));
            };
            let cert = Certificate::from_bytes(contents, cache)?;
            return Ok((cert, eof));
        }
    }

    /// Extract the DER encoded certificate.
    pub fn as_der_bytes(&self) -> &[u8] {
        let (data, len) = unsafe {
            // Safety: `self.0` is still valid.
            (
                bssl_sys::CRYPTO_BUFFER_data(self.ptr()),
                bssl_sys::CRYPTO_BUFFER_len(self.ptr()),
            )
        };
        if data.is_null() || len == 0 || len >= isize::MAX as usize {
            return &[];
        }
        unsafe {
            // Safety: `data` will be outlived by `self`
            core::slice::from_raw_parts(data, len)
        }
    }
}

crypto_buffer_wrapper! {
    /// A dumb handle to a **possibly valid** OCSP Response.
    pub struct OcspResponse
}

crypto_buffer_wrapper! {
    /// A dumb handle to a **possibly valid** certificate property list.
    pub struct CertificatePropertyList
}

crypto_buffer_wrapper! {
    /// A dumb handle to a **possibly valid** certificate timestamp list.
    ///
    /// This list must contain at least one Signed Certificate Timestamp, or SCT for short,
    /// serialised as *[SignedCertificateTimestampList]*.
    ///
    /// The list begins with a length encoded as a big-endian 16-byte unsigned integer.
    /// Thereon follows a simple concatenation of SCTs.
    /// Each SCT is prefixed with a length encoded as a big-endian 16-byte unsigned integer.
    ///
    /// [SignedCertificateTimestampList]: <https://tools.ietf.org/html/rfc6962#section-3.3>
    pub struct SignedCertificateTimestampList
}

crypto_buffer_wrapper! {
    /// A dumb handle to a **possibly valid** delegated credential.
    ///
    /// By construction we do not validate if the contained bytes actually conforms to
    /// the structure of `DelegatedCredential` as stipulated by [RFC 9345].
    ///
    /// [RFC 9345]: <https://tools.ietf.org/html/rfc9345>
    pub struct DelegatedCredential
}

bssl_macros::bssl_enum! {
    /// [IANA] designation of TLS signature algorithms.
    ///
    /// [IANA]: https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-16
    #[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
    pub enum SignatureAlgorithm: u16 {
        /// IANA entry `rsa_pkcs1_sha256`
        RsaPkcs1Sha256 = bssl_sys::SSL_SIGN_RSA_PKCS1_SHA256 as u16,
        /// IANA entry `rsa_pkcs1_sha384`
        RsaPkcs1Sha384 = bssl_sys::SSL_SIGN_RSA_PKCS1_SHA384 as u16,
        /// IANA entry `rsa_pkcs1_sha512`
        RsaPkcs1Sha512 = bssl_sys::SSL_SIGN_RSA_PKCS1_SHA512 as u16,
        /// IANA entry `ecdsa_secp256r1_sha256`
        EcdsaSecp256r1Sha256 = bssl_sys::SSL_SIGN_ECDSA_SECP256R1_SHA256 as u16,
        /// IANA entry `ecdsa_secp384r1_sha384`
        EcdsaSecp384r1Sha384 = bssl_sys::SSL_SIGN_ECDSA_SECP384R1_SHA384 as u16,
        /// IANA entry `ecdsa_secp521r1_sha512`
        EcdsaSecp521r1Sha512 = bssl_sys::SSL_SIGN_ECDSA_SECP521R1_SHA512 as u16,
        /// IANA entry `rsa_pss_rsae_sha256`
        RsaPssRsaeSha256 = bssl_sys::SSL_SIGN_RSA_PSS_RSAE_SHA256 as u16,
        /// IANA entry `rsa_pss_rsae_sha384`
        RsaPssRsaeSha384 = bssl_sys::SSL_SIGN_RSA_PSS_RSAE_SHA384 as u16,
        /// IANA entry `rsa_pss_rsae_sha512`
        RsaPssRsaeSha512 = bssl_sys::SSL_SIGN_RSA_PSS_RSAE_SHA512 as u16,
        /// IANA entry `ed25519`
        Ed25519 = bssl_sys::SSL_SIGN_ED25519 as u16,
        /// IANA entry `ML-DSA-44`
        Mldsa44 = bssl_sys::SSL_SIGN_ML_DSA_44 as u16,
        /// IANA entry `ML-DSA-65`
        Mldsa65 = bssl_sys::SSL_SIGN_ML_DSA_65 as u16,
        /// IANA entry `ML-DSA-87`
        Mldsa87 = bssl_sys::SSL_SIGN_ML_DSA_87 as u16,
    }
}

bssl_macros::bssl_enum! {
    /// Certificate verification mode
    pub enum CertificateVerificationMode: i8 {
        /// Verifies the server certificate on a client but does not make errors fatal.
        None = bssl_sys::SSL_VERIFY_NONE as i8,
        /// Verifies the server certificate on a client and makes errors fatal.
        PeerCertRequested = bssl_sys::SSL_VERIFY_PEER as i8,
        /// Configures a server to request a client certificate and **reject** connections if
        /// the client declines to send a certificate.
        PeerCertMandatory =
            (bssl_sys::SSL_VERIFY_FAIL_IF_NO_PEER_CERT | bssl_sys::SSL_VERIFY_PEER) as i8,
    }
}

impl TryFrom<c_int> for CertificateVerificationMode {
    type Error = c_int;
    fn try_from(mode: c_int) -> Result<Self, Self::Error> {
        let Ok(value) = i8::try_from(mode) else {
            return Err(mode);
        };
        if let Ok(mode) = Self::try_from(value) {
            Ok(mode)
        } else {
            Err(mode)
        }
    }
}

#[cfg(test)]
mod tests;
