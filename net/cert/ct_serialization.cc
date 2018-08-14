// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/ct_serialization.h"

#include <stdint.h>

#include <algorithm>
#include <limits>

#include "base/logging.h"
#include "base/numerics/safe_math.h"
#include "crypto/sha2.h"
#include "net/cert/merkle_tree_leaf.h"
#include "net/cert/signed_certificate_timestamp.h"
#include "net/cert/signed_tree_head.h"

namespace net {

namespace ct {

namespace {

// Note: length is always specified in bytes.
// CT protocol version length
const size_t kVersionLength = 1;

// Common V1 struct members
const size_t kTimestampLength = 8;
const size_t kSignedEntryTypeLength = 2;
const size_t kAsn1CertificateLengthBytes = 3;
const size_t kTbsCertificateLengthBytes = 3;
const size_t kExtensionsLengthBytes = 2;

// Members of a V1 SCT
const size_t kLogIdLength = crypto::kSHA256Length;
const size_t kHashAlgorithmLength = 1;
const size_t kSigAlgorithmLength = 1;
const size_t kSignatureLengthBytes = 2;

// Members of the digitally-signed struct of a V1 SCT
const size_t kSignatureTypeLength = 1;

const size_t kSCTListLengthBytes = 2;
const size_t kSerializedSCTLengthBytes = 2;

// Members of digitally-signed struct of a STH
const size_t kTreeSizeLength = 8;

// Members of a V1 MerkleTreeLeaf
const size_t kMerkleLeafTypeLength = 1;
const size_t kIssuerKeyHashLength = crypto::kSHA256Length;

enum SignatureType {
  SIGNATURE_TYPE_CERTIFICATE_TIMESTAMP = 0,
  TREE_HASH = 1,
};

// Reads a TLS-encoded variable length unsigned integer from |in|.
// The integer is expected to be in big-endian order, which is used by TLS.
// The bytes read from |in| are discarded (i.e. |in|'s prefix removed)
// |length| indicates the size (in bytes) of the integer. On success, returns
// true and stores the result in |*out|.
template <typename T>
bool ReadUint(size_t length, base::StringPiece* in, T* out) {
  if (in->size() < length)
    return false;
  DCHECK_NE(length, 0u);
  DCHECK_LE(length, sizeof(T));

  T result = static_cast<uint8_t>((*in)[0]);
  // This loop only executes if sizeof(T) > 1, because the first operation is
  // to shift left by 1 byte, which is undefined behaviour if T is a 1 byte
  // integer.
  for (size_t i = 1; i < length; ++i) {
    result = (result << 8) | static_cast<uint8_t>((*in)[i]);
  }
  in->remove_prefix(length);
  *out = result;
  return true;
}

// Reads a TLS-encoded field length from |in|.
// The bytes read from |in| are discarded (i.e. |in|'s prefix removed).
// |prefix_length| indicates the bytes needed to represent the length (e.g. 3).
// Max |prefix_length| is 8.
// success, returns true and stores the result in |*out|.
bool ReadLength(size_t prefix_length, base::StringPiece* in, size_t* out) {
  uint64_t length = 0;
  if (!ReadUint(prefix_length, in, &length))
    return false;
  base::CheckedNumeric<size_t> checked_length = length;
  if (!checked_length.IsValid())
    return false;
  *out = checked_length.ValueOrDie();
  return true;
}

// Reads |length| bytes from |*in|. If |*in| is too small, returns false.
// The bytes read from |in| are discarded (i.e. |in|'s prefix removed)
bool ReadFixedBytes(size_t length,
                    base::StringPiece* in,
                    base::StringPiece* out) {
  if (in->length() < length)
    return false;
  out->set(in->data(), length);
  in->remove_prefix(length);
  return true;
}

// Reads a length-prefixed variable amount of bytes from |in|, updating |out|
// on success. |prefix_length| indicates the number of bytes needed to represent
// the length.
// The bytes read from |in| are discarded (i.e. |in|'s prefix removed)
bool ReadVariableBytes(size_t prefix_length,
                       base::StringPiece* in,
                       base::StringPiece* out) {
  size_t length = 0;
  if (!ReadLength(prefix_length, in, &length))
    return false;
  return ReadFixedBytes(length, in, out);
}

// Reads a variable-length list that has been TLS encoded.
// The bytes read from |in| are discarded (i.e. |in|'s prefix removed)
// |max_list_length| contains the overall length of the encoded list.
// |max_item_length| contains the maximum length of a single item.
// On success, returns true and updates |*out| with the encoded list.
bool ReadList(size_t max_list_length,
              size_t max_item_length,
              base::StringPiece* in,
              std::vector<base::StringPiece>* out) {
  std::vector<base::StringPiece> result;

  base::StringPiece list_data;
  if (!ReadVariableBytes(max_list_length, in, &list_data))
    return false;

  while (!list_data.empty()) {
    base::StringPiece list_item;
    if (!ReadVariableBytes(max_item_length, &list_data, &list_item)) {
      DVLOG(1) << "Failed to read item in list.";
      return false;
    }
    if (list_item.empty()) {
      DVLOG(1) << "Empty item in list";
      return false;
    }
    result.push_back(list_item);
  }

  result.swap(*out);
  return true;
}

// Checks and converts a hash algorithm.
// |in| is the numeric representation of the algorithm.
// If the hash algorithm value is in a set of known values, fills in |out| and
// returns true. Otherwise, returns false.
bool ConvertHashAlgorithm(unsigned in, DigitallySigned::HashAlgorithm* out) {
  switch (in) {
    case DigitallySigned::HASH_ALGO_NONE:
    case DigitallySigned::HASH_ALGO_MD5:
    case DigitallySigned::HASH_ALGO_SHA1:
    case DigitallySigned::HASH_ALGO_SHA224:
    case DigitallySigned::HASH_ALGO_SHA256:
    case DigitallySigned::HASH_ALGO_SHA384:
    case DigitallySigned::HASH_ALGO_SHA512:
      break;
    default:
      return false;
  }
  *out = static_cast<DigitallySigned::HashAlgorithm>(in);
  return true;
}

// Checks and converts a signing algorithm.
// |in| is the numeric representation of the algorithm.
// If the signing algorithm value is in a set of known values, fills in |out|
// and returns true. Otherwise, returns false.
bool ConvertSignatureAlgorithm(
    unsigned in,
    DigitallySigned::SignatureAlgorithm* out) {
  switch (in) {
    case DigitallySigned::SIG_ALGO_ANONYMOUS:
    case DigitallySigned::SIG_ALGO_RSA:
    case DigitallySigned::SIG_ALGO_DSA:
    case DigitallySigned::SIG_ALGO_ECDSA:
      break;
    default:
      return false;
  }
  *out = static_cast<DigitallySigned::SignatureAlgorithm>(in);
  return true;
}

// Writes a TLS-encoded variable length unsigned integer to |output|.
// |length| indicates the size (in bytes) of the integer. This must be able to
// accomodate |value|.
// |value| the value itself to be written.
void WriteUint(size_t length, uint64_t value, std::string* output) {
  // Check that |value| fits into |length| bytes.
  DCHECK(length >= sizeof(value) || value >> (length * 8) == 0);

  for (; length > 0; --length) {
    output->push_back((value >> ((length - 1) * 8)) & 0xFF);
  }
}

// Writes an array to |output| from |input|.
// Should be used in one of two cases:
// * The length of |input| has already been encoded into the |output| stream.
// * The length of |input| is fixed and the reader is expected to specify that
// length when reading.
// If the length of |input| is dynamic and data is expected to follow it,
// WriteVariableBytes must be used.
// Returns the number of bytes written (the length of |input|).
size_t WriteEncodedBytes(const base::StringPiece& input, std::string* output) {
  input.AppendToString(output);
  return input.size();
}

// Writes a variable-length array to |output|.
// |prefix_length| indicates the number of bytes needed to represent the length.
// |input| is the array itself.
// If 1 <= |prefix_length| <= 8 and the size of |input| is less than
// 2^|prefix_length| - 1, encode the length and data and return true.
// Otherwise, return false.
bool WriteVariableBytes(size_t prefix_length,
                        const base::StringPiece& input,
                        std::string* output) {
  DCHECK_GE(prefix_length, 1u);
  DCHECK_LE(prefix_length, 8u);

  uint64_t input_size = input.size();
  uint64_t max_input_size = (prefix_length == 8)
                                ? UINT64_MAX
                                : ((UINT64_C(1) << (prefix_length * 8)) - 1);

  if (input_size > max_input_size)
    return false;

  WriteUint(prefix_length, input_size, output);
  WriteEncodedBytes(input, output);

  return true;
}

// Writes a SignedEntryData of type X.509 cert to |output|.
// |input| is the SignedEntryData containing the certificate.
// Returns true if the leaf_certificate in the SignedEntryData does not exceed
// kMaxAsn1CertificateLength and so can be written to |output|.
bool EncodeAsn1CertSignedEntry(const SignedEntryData& input,
                               std::string* output) {
  return WriteVariableBytes(kAsn1CertificateLengthBytes,
                            input.leaf_certificate, output);
}

// Writes a SignedEntryData of type PreCertificate to |output|.
// |input| is the SignedEntryData containing the TBSCertificate and issuer key
// hash. Returns true if the TBSCertificate component in the SignedEntryData
// does not exceed kMaxTbsCertificateLength and so can be written to |output|.
bool EncodePrecertSignedEntry(const SignedEntryData& input,
                              std::string* output) {
  WriteEncodedBytes(
      base::StringPiece(
          reinterpret_cast<const char*>(input.issuer_key_hash.data),
          kIssuerKeyHashLength),
      output);
  return WriteVariableBytes(kTbsCertificateLengthBytes,
                            input.tbs_certificate, output);
}

}  // namespace

bool EncodeDigitallySigned(const DigitallySigned& input,
                           std::string* output) {
  WriteUint(kHashAlgorithmLength, input.hash_algorithm, output);
  WriteUint(kSigAlgorithmLength, input.signature_algorithm,
            output);
  return WriteVariableBytes(kSignatureLengthBytes, input.signature_data,
                            output);
}

bool DecodeDigitallySigned(base::StringPiece* input,
                           DigitallySigned* output) {
  unsigned hash_algo;
  unsigned sig_algo;
  base::StringPiece sig_data;

  if (!ReadUint(kHashAlgorithmLength, input, &hash_algo) ||
      !ReadUint(kSigAlgorithmLength, input, &sig_algo) ||
      !ReadVariableBytes(kSignatureLengthBytes, input, &sig_data)) {
    return false;
  }

  DigitallySigned result;
  if (!ConvertHashAlgorithm(hash_algo, &result.hash_algorithm)) {
    DVLOG(1) << "Invalid hash algorithm " << hash_algo;
    return false;
  }
  if (!ConvertSignatureAlgorithm(sig_algo, &result.signature_algorithm)) {
    DVLOG(1) << "Invalid signature algorithm " << sig_algo;
    return false;
  }
  sig_data.CopyToString(&result.signature_data);

  *output = result;
  return true;
}

bool EncodeSignedEntry(const SignedEntryData& input, std::string* output) {
  WriteUint(kSignedEntryTypeLength, input.type, output);
  switch (input.type) {
    case SignedEntryData::LOG_ENTRY_TYPE_X509:
      return EncodeAsn1CertSignedEntry(input, output);
    case SignedEntryData::LOG_ENTRY_TYPE_PRECERT:
      return EncodePrecertSignedEntry(input, output);
  }
  return false;
}

static bool ReadTimeSinceEpoch(base::StringPiece* input, base::Time* output) {
  uint64_t time_since_epoch = 0;
  if (!ReadUint(kTimestampLength, input, &time_since_epoch))
    return false;

  base::CheckedNumeric<int64_t> time_since_epoch_signed = time_since_epoch;

  if (!time_since_epoch_signed.IsValid()) {
    DVLOG(1) << "Timestamp value too big to cast to int64_t: "
             << time_since_epoch;
    return false;
  }

  *output =
      base::Time::UnixEpoch() +
      base::TimeDelta::FromMilliseconds(time_since_epoch_signed.ValueOrDie());

  return true;
}

static void WriteTimeSinceEpoch(const base::Time& timestamp,
                                std::string* output) {
  base::TimeDelta time_since_epoch = timestamp - base::Time::UnixEpoch();
  WriteUint(kTimestampLength, time_since_epoch.InMilliseconds(), output);
}

bool EncodeTreeLeaf(const MerkleTreeLeaf& leaf, std::string* output) {
  WriteUint(kVersionLength, 0, output);         // version: 1
  WriteUint(kMerkleLeafTypeLength, 0, output);  // type: timestamped entry
  WriteTimeSinceEpoch(leaf.timestamp, output);
  if (!EncodeSignedEntry(leaf.signed_entry, output))
    return false;
  if (!WriteVariableBytes(kExtensionsLengthBytes, leaf.extensions, output))
    return false;

  return true;
}

bool EncodeV1SCTSignedData(const base::Time& timestamp,
                           const std::string& serialized_log_entry,
                           const std::string& extensions,
                           std::string* output) {
  WriteUint(kVersionLength, SignedCertificateTimestamp::V1,
            output);
  WriteUint(kSignatureTypeLength, SIGNATURE_TYPE_CERTIFICATE_TIMESTAMP,
            output);
  WriteTimeSinceEpoch(timestamp, output);
  // NOTE: serialized_log_entry must already be serialized and contain the
  // length as the prefix.
  WriteEncodedBytes(serialized_log_entry, output);
  return WriteVariableBytes(kExtensionsLengthBytes, extensions, output);
}

void EncodeTreeHeadSignature(const SignedTreeHead& signed_tree_head,
                             std::string* output) {
  WriteUint(kVersionLength, signed_tree_head.version, output);
  WriteUint(kSignatureTypeLength, TREE_HASH, output);
  WriteTimeSinceEpoch(signed_tree_head.timestamp, output);
  WriteUint(kTreeSizeLength, signed_tree_head.tree_size, output);
  WriteEncodedBytes(
      base::StringPiece(signed_tree_head.sha256_root_hash, kSthRootHashLength),
      output);
}

bool DecodeSCTList(base::StringPiece input,
                   std::vector<base::StringPiece>* output) {
  std::vector<base::StringPiece> result;
  if (!ReadList(kSCTListLengthBytes, kSerializedSCTLengthBytes, &input,
                &result)) {
    return false;
  }

  if (!input.empty() || result.empty())
    return false;
  output->swap(result);
  return true;
}

bool DecodeSignedCertificateTimestamp(
    base::StringPiece* input,
    scoped_refptr<SignedCertificateTimestamp>* output) {
  scoped_refptr<SignedCertificateTimestamp> result(
      new SignedCertificateTimestamp());
  unsigned version;
  if (!ReadUint(kVersionLength, input, &version))
    return false;
  if (version != SignedCertificateTimestamp::V1) {
    DVLOG(1) << "Unsupported/invalid version " << version;
    return false;
  }

  result->version = SignedCertificateTimestamp::V1;
  base::StringPiece log_id;
  base::StringPiece extensions;
  if (!ReadFixedBytes(kLogIdLength, input, &log_id) ||
      !ReadTimeSinceEpoch(input, &result->timestamp) ||
      !ReadVariableBytes(kExtensionsLengthBytes, input, &extensions) ||
      !DecodeDigitallySigned(input, &result->signature)) {
    return false;
  }

  log_id.CopyToString(&result->log_id);
  extensions.CopyToString(&result->extensions);
  output->swap(result);
  return true;
}

void EncodeSignedCertificateTimestamp(
    const scoped_refptr<ct::SignedCertificateTimestamp>& input,
    std::string* output) {
  // This function only supports serialization of V1 SCTs.
  DCHECK_EQ(SignedCertificateTimestamp::V1, input->version);
  WriteUint(kVersionLength, input->version, output);
  DCHECK_EQ(kLogIdLength, input->log_id.size());
  WriteEncodedBytes(
      base::StringPiece(reinterpret_cast<const char*>(input->log_id.data()),
                        kLogIdLength),
      output);
  WriteTimeSinceEpoch(input->timestamp, output);
  WriteVariableBytes(kExtensionsLengthBytes, input->extensions, output);
  EncodeDigitallySigned(input->signature, output);
}

bool EncodeSCTListForTesting(const base::StringPiece& sct,
                             std::string* output) {
  std::string encoded_sct;
  return WriteVariableBytes(kSerializedSCTLengthBytes, sct, &encoded_sct) &&
         WriteVariableBytes(kSCTListLengthBytes, encoded_sct, output);
}

}  // namespace ct

}  // namespace net
