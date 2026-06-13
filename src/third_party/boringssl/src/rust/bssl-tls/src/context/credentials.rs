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

use alloc::sync::Arc;

use bssl_x509::{
    params::CertificateVerificationParams,
    store::X509Store, //
};

use super::TlsContextBuilder;
use crate::{
    check_lib_error,
    config::ConfigurationError,
    context::{
        CertificateCache,
        SupportedMode, //
    },
    credentials::{
        CertificateVerificationMode,
        SignatureAlgorithm,
        TlsCredential, //
    },
    errors::Error,
    ffi::slice_into_ffi_raw_parts,
    has_duplicates, //
};

/// # Credentials
impl<M> TlsContextBuilder<M>
where
    M: SupportedMode,
{
    /// Set the certificate cache.
    ///
    /// This certificate cache will be used
    pub fn with_certificate_cache(&mut self, cache: Option<Arc<CertificateCache>>) -> &mut Self {
        let ctx = self.ptr();
        if let Some(cache) = &cache {
            unsafe {
                // Safety: `CertificateCache` is `Send + Sync`
                bssl_sys::SSL_CTX_set0_buffer_pool(ctx, cache.ptr() as _);
            }
        } else {
            unsafe {
                // Safety: we just detach the buffer pool before any active pointer into it is created.
                bssl_sys::SSL_CTX_set0_buffer_pool(ctx, core::ptr::null_mut());
            }
        }
        self.cert_cache = cache;
        self
    }

    /// Set certificate verification mode.
    ///
    /// # Client certificate verification for servers, mutual TLS
    ///
    /// Server can choose to request a certificate from the client by setting `mode` to
    /// - [`CertificateVerificationMode::PeerCertRequested`] which may still let handshake complete
    ///   if the certificate request by the server is not fulfilled.
    /// - [`CertificateVerificationMode::PeerCertMandatory`] which will abort handshake if
    ///   the request is not fulfilled.
    pub fn with_certificate_verification_mode(
        &mut self,
        mode: CertificateVerificationMode,
    ) -> &mut Self {
        let conn = self.ptr();
        unsafe {
            // Safety: we only uninstall the vtable.
            bssl_sys::SSL_CTX_set_custom_verify(conn, mode as _, None);
        }
        self
    }

    /// Append `credential` to the list of credentials of this context.
    pub fn with_credential(&mut self, credential: TlsCredential) -> Result<&mut Self, Error> {
        check_lib_error!(unsafe {
            // Safety:
            // - the validity of the handle `self.0` is witnessed by `self`.
            // - the method will bump the refcount of the credential for ownership.
            bssl_sys::SSL_CTX_add1_credential(self.ptr(), credential.ptr())
        });
        Ok(self)
    }
}

/// # Certificate verification
impl<M> TlsContextBuilder<M> {
    /// Set certificate verification parameters.
    pub fn with_certificate_verification_params(
        &mut self,
        params: &CertificateVerificationParams,
    ) -> Result<&mut Self, Error> {
        check_lib_error!(unsafe {
            // Safety:
            // - the validity of the handle `self.0` is witnessed by `self`.
            // - `SSL_CTX_set1_param` bumps the ref-count on the `params`.
            bssl_sys::SSL_CTX_set1_param(self.ptr(), params.as_ptr())
        });
        Ok(self)
    }

    /// Enable signed certificate timestamps.
    ///
    /// This method will instruct the client connections to request Signed Certificate Timestamps.
    /// See [RFC 6962] for more information.
    ///
    /// [RFC 6962]: https://datatracker.ietf.org/doc/html/rfc6962
    pub fn enable_signed_certificate_timestamps(&mut self) -> &mut Self {
        unsafe {
            // Safety: the validity of the handle `self.0` is witnessed by `self`.
            bssl_sys::SSL_CTX_enable_signed_cert_timestamps(self.ptr());
        }
        self
    }

    /// Set certificate verification store.
    pub fn with_certificate_store(&mut self, store: &X509Store) -> &mut Self {
        unsafe {
            // Safety:
            // - the validity of the handle `self.0` is witnessed by `self`.
            // - `SSL_CTX_set1_verify_cert_store` bumps the ref-count on `store`.
            assert_eq!(
                bssl_sys::SSL_CTX_set1_verify_cert_store(self.ptr(), store.as_mut_ptr()),
                1,
            );
        }
        self
    }

    /// Set a preference list of signature algorithms for verification.
    ///
    /// This method returns [`ConfigurationError::InvalidParameters`] if the list of algorithms
    /// contains duplicate entries.
    pub fn with_signature_verification_algorithm_preferences(
        &mut self,
        algs: &[SignatureAlgorithm],
    ) -> Result<&mut Self, Error> {
        let algs: &[u16] = unsafe {
            // Safety: `SignatureAlgorithm` has a `repr(u16)` and maps to preferences correctly
            // by construction.
            core::mem::transmute(algs)
        };
        if has_duplicates(algs) {
            return Err(Error::Configuration(ConfigurationError::InvalidParameters));
        }

        let (prefs, prefs_len) = slice_into_ffi_raw_parts(algs);
        check_lib_error!(unsafe {
            // Safety: the validity of the handle `self.0` is witnessed by `self`.
            bssl_sys::SSL_CTX_set_verify_algorithm_prefs(self.ptr(), prefs, prefs_len)
        });
        Ok(self)
    }

    /// Set a preference list of algorithms for signing.
    ///
    /// This method returns [`ConfigurationError::InvalidParameters`] if the list of algorithms
    /// contains duplicate entries.
    pub fn with_signing_algorithm_preferences(
        &mut self,
        algs: &[SignatureAlgorithm],
    ) -> Result<&mut Self, Error> {
        let algs: &[u16] = unsafe {
            // Safety: `SignatureAlgorithm` has a `repr(u16)` and maps to preferences correctly
            // by construction.
            core::mem::transmute(algs)
        };
        if has_duplicates(algs) {
            return Err(Error::Configuration(ConfigurationError::InvalidParameters));
        }

        let (prefs, prefs_len) = slice_into_ffi_raw_parts(algs);
        check_lib_error!(unsafe {
            // Safety: the validity of the handle `self.0` is witnessed by `self`.
            bssl_sys::SSL_CTX_set_signing_algorithm_prefs(self.ptr(), prefs, prefs_len)
        });
        Ok(self)
    }
}
