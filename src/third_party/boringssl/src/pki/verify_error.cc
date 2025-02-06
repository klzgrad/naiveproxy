/* Copyright 2024 The BoringSSL Authors
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

#include <openssl/base.h>
#include <openssl/pki/verify_error.h>

BSSL_NAMESPACE_BEGIN

VerifyError::VerifyError(StatusCode code, ptrdiff_t offset,
                         std::string diagnostic)
    : offset_(offset), code_(code), diagnostic_(std::move(diagnostic)) {}

const std::string &VerifyError::DiagnosticString() const { return diagnostic_; }

ptrdiff_t VerifyError::Index() const { return offset_; }

VerifyError::StatusCode VerifyError::Code() const { return code_; }

BSSL_NAMESPACE_END
