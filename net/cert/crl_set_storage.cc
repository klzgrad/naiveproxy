// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/crl_set_storage.h"

#include <memory>

#include "base/base64.h"
#include "base/format_macros.h"
#include "base/json/json_reader.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "crypto/sha2.h"
#include "net/base/trace_constants.h"
#include "third_party/zlib/zlib.h"

namespace net {

// Decompress zlib decompressed |in| into |out|. |out_len| is the number of
// bytes at |out| and must be exactly equal to the size of the decompressed
// data.
static bool DecompressZlib(uint8_t* out, int out_len, base::StringPiece in) {
  z_stream z;
  memset(&z, 0, sizeof(z));

  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
  z.avail_in = in.size();
  z.next_out = reinterpret_cast<Bytef*>(out);
  z.avail_out = out_len;

  if (inflateInit(&z) != Z_OK)
    return false;
  bool ret = false;
  int r = inflate(&z, Z_FINISH);
  if (r != Z_STREAM_END)
    goto err;
  if (z.avail_in || z.avail_out)
    goto err;
  ret = true;

 err:
  inflateEnd(&z);
  return ret;
}

// CRLSet format:
//
// uint16le header_len
// byte[header_len] header_bytes
// repeated {
//   byte[32] parent_spki_sha256
//   uint32le num_serials
//   [num_serials] {
//     uint8_t serial_length;
//     byte[serial_length] serial;
//   }
//
// header_bytes consists of a JSON dictionary with the following keys:
//   Version (int): currently 0
//   ContentType (string): "CRLSet" or "CRLSetDelta" (magic value)
//   DeltaFrom (int32_t): if this is a delta update (see below), then this
//       contains the sequence number of the base CRLSet.
//   Sequence (int32_t): the monotonic sequence number of this CRL set.
//
// A delta CRLSet is similar to a CRLSet:
//
// struct CompressedChanges {
//    uint32le uncompressed_size
//    uint32le compressed_size
//    byte[compressed_size] zlib_data
// }
//
// uint16le header_len
// byte[header_len] header_bytes
// CompressedChanges crl_changes
// [crl_changes.uncompressed_size] {
//   switch (crl_changes[i]) {
//   case 0:
//     // CRL is the same
//   case 1:
//     // New CRL inserted
//     // See CRL structure from the non-delta format
//   case 2:
//     // CRL deleted
//   case 3:
//     // CRL changed
//     CompressedChanges serials_changes
//     [serials_changes.uncompressed_size] {
//       switch (serials_changes[i]) {
//       case 0:
//         // the serial is the same
//       case 1:
//         // serial inserted
//         uint8_t serial_length
//         byte[serial_length] serial
//       case 2:
//         // serial deleted
//       }
//     }
//   }
// }
//
// A delta CRLSet applies to a specific CRL set as given in the
// header's "DeltaFrom" value. The delta describes the changes to each CRL
// in turn with a zlib compressed array of options: either the CRL is the same,
// a new CRL is inserted, the CRL is deleted or the CRL is updated. In the case
// of an update, the serials in the CRL are considered in the same fashion
// except there is no delta update of a serial number: they are either
// inserted, deleted or left the same.

// ReadHeader reads the header (including length prefix) from |data| and
// updates |data| to remove the header on return. Caller takes ownership of the
// returned pointer.
static base::DictionaryValue* ReadHeader(base::StringPiece* data) {
  uint16_t header_len;
  if (data->size() < sizeof(header_len))
    return NULL;
  // Assumes little-endian.
  memcpy(&header_len, data->data(), sizeof(header_len));
  data->remove_prefix(sizeof(header_len));

  if (data->size() < header_len)
    return NULL;

  const base::StringPiece header_bytes(data->data(), header_len);
  data->remove_prefix(header_len);

  std::unique_ptr<base::Value> header =
      base::JSONReader::Read(header_bytes, base::JSON_ALLOW_TRAILING_COMMAS);
  if (header.get() == NULL)
    return NULL;

  if (!header->IsType(base::Value::Type::DICTIONARY))
    return NULL;
  return static_cast<base::DictionaryValue*>(header.release());
}

// kCurrentFileVersion is the version of the CRLSet file format that we
// currently implement.
static const int kCurrentFileVersion = 0;

static bool ReadCRL(base::StringPiece* data, std::string* out_parent_spki_hash,
                    std::vector<std::string>* out_serials) {
  if (data->size() < crypto::kSHA256Length)
    return false;
  out_parent_spki_hash->assign(data->data(), crypto::kSHA256Length);
  data->remove_prefix(crypto::kSHA256Length);

  uint32_t num_serials;
  if (data->size() < sizeof(num_serials))
    return false;
  // Assumes little endian.
  memcpy(&num_serials, data->data(), sizeof(num_serials));
  data->remove_prefix(sizeof(num_serials));

  if (num_serials > 32 * 1024 * 1024)  // Sanity check.
    return false;

  out_serials->reserve(num_serials);

  for (uint32_t i = 0; i < num_serials; ++i) {
    if (data->size() < sizeof(uint8_t))
      return false;

    uint8_t serial_length = data->data()[0];
    data->remove_prefix(sizeof(uint8_t));

    if (data->size() < serial_length)
      return false;

    out_serials->push_back(std::string());
    out_serials->back().assign(data->data(), serial_length);
    data->remove_prefix(serial_length);
  }

  return true;
}

// static
bool CRLSetStorage::CopyBlockedSPKIsFromHeader(
    CRLSet* crl_set,
    base::DictionaryValue* header_dict) {
  base::ListValue* blocked_spkis_list = NULL;
  if (!header_dict->GetList("BlockedSPKIs", &blocked_spkis_list)) {
    // BlockedSPKIs is optional, so it's fine if we don't find it.
    return true;
  }

  crl_set->blocked_spkis_.clear();
  crl_set->blocked_spkis_.reserve(blocked_spkis_list->GetSize());

  std::string spki_sha256_base64;

  for (size_t i = 0; i < blocked_spkis_list->GetSize(); ++i) {
    spki_sha256_base64.clear();

    if (!blocked_spkis_list->GetString(i, &spki_sha256_base64))
      return false;

    crl_set->blocked_spkis_.push_back(std::string());
    if (!base::Base64Decode(spki_sha256_base64,
                            &crl_set->blocked_spkis_.back())) {
      crl_set->blocked_spkis_.pop_back();
      return false;
    }
  }

  return true;
}

// kMaxUncompressedChangesLength is the largest changes array that we'll
// accept. This bounds the number of CRLs in the CRLSet as well as the number
// of serial numbers in a given CRL.
static const unsigned kMaxUncompressedChangesLength = 1024 * 1024;

static bool ReadChanges(base::StringPiece* data,
                        std::vector<uint8_t>* out_changes) {
  uint32_t uncompressed_size, compressed_size;
  if (data->size() < sizeof(uncompressed_size) + sizeof(compressed_size))
    return false;
  // Assumes little endian.
  memcpy(&uncompressed_size, data->data(), sizeof(uncompressed_size));
  data->remove_prefix(sizeof(uncompressed_size));
  memcpy(&compressed_size, data->data(), sizeof(compressed_size));
  data->remove_prefix(sizeof(compressed_size));

  if (uncompressed_size > kMaxUncompressedChangesLength)
    return false;
  if (data->size() < compressed_size)
    return false;

  out_changes->clear();
  if (uncompressed_size == 0)
    return true;

  out_changes->resize(uncompressed_size);
  base::StringPiece compressed(data->data(), compressed_size);
  data->remove_prefix(compressed_size);
  return DecompressZlib(&(*out_changes)[0], uncompressed_size, compressed);
}

// These are the range coder symbols used in delta updates.
enum {
  SYMBOL_SAME = 0,
  SYMBOL_INSERT = 1,
  SYMBOL_DELETE = 2,
  SYMBOL_CHANGED = 3,
};

static bool ReadDeltaCRL(base::StringPiece* data,
                         const std::vector<std::string>& old_serials,
                         std::vector<std::string>* out_serials) {
  std::vector<uint8_t> changes;
  if (!ReadChanges(data, &changes))
    return false;

  size_t i = 0;
  for (std::vector<uint8_t>::const_iterator k = changes.begin();
       k != changes.end(); ++k) {
    if (*k == SYMBOL_SAME) {
      if (i >= old_serials.size())
        return false;
      out_serials->push_back(old_serials[i]);
      i++;
    } else if (*k == SYMBOL_INSERT) {
      if (data->size() < sizeof(uint8_t))
        return false;
      uint8_t serial_length = data->data()[0];
      data->remove_prefix(sizeof(uint8_t));

      if (data->size() < serial_length)
        return false;
      const std::string serial(data->data(), serial_length);
      data->remove_prefix(serial_length);

      out_serials->push_back(serial);
    } else if (*k == SYMBOL_DELETE) {
      if (i >= old_serials.size())
        return false;
      i++;
    } else {
      NOTREACHED();
      return false;
    }
  }

  if (i != old_serials.size())
    return false;
  return true;
}

// static
bool CRLSetStorage::Parse(base::StringPiece data,
                          scoped_refptr<CRLSet>* out_crl_set) {
  TRACE_EVENT0(kNetTracingCategory, "CRLSetStorage::Parse");
  // Other parts of Chrome assume that we're little endian, so we don't lose
  // anything by doing this.
#if defined(__BYTE_ORDER)
  // Linux check
  static_assert(__BYTE_ORDER == __LITTLE_ENDIAN, "assumes little endian");
#elif defined(__BIG_ENDIAN__)
  // Mac check
  #error assumes little endian
#endif

  std::unique_ptr<base::DictionaryValue> header_dict(ReadHeader(&data));
  if (!header_dict.get())
    return false;

  std::string contents;
  if (!header_dict->GetString("ContentType", &contents))
    return false;
  if (contents != "CRLSet")
    return false;

  int version;
  if (!header_dict->GetInteger("Version", &version) ||
      version != kCurrentFileVersion) {
    return false;
  }

  int sequence;
  if (!header_dict->GetInteger("Sequence", &sequence))
    return false;

  double not_after;
  if (!header_dict->GetDouble("NotAfter", &not_after)) {
    // NotAfter is optional for now.
    not_after = 0;
  }
  if (not_after < 0)
    return false;

  scoped_refptr<CRLSet> crl_set(new CRLSet());
  crl_set->sequence_ = static_cast<uint32_t>(sequence);
  crl_set->not_after_ = static_cast<uint64_t>(not_after);
  crl_set->crls_.reserve(64);  // Value observed experimentally.

  for (size_t crl_index = 0; !data.empty(); crl_index++) {
    // Speculatively push back a pair and pass it to ReadCRL() to avoid
    // unnecessary copies.
    crl_set->crls_.push_back(
        std::make_pair(std::string(), std::vector<std::string>()));
    std::pair<std::string, std::vector<std::string> >* const back_pair =
        &crl_set->crls_.back();

    if (!ReadCRL(&data, &back_pair->first, &back_pair->second)) {
      // Undo the speculative push_back() performed above.
      crl_set->crls_.pop_back();
      return false;
    }

    crl_set->crls_index_by_issuer_[back_pair->first] = crl_index;
  }

  if (!CopyBlockedSPKIsFromHeader(crl_set.get(), header_dict.get()))
    return false;

  *out_crl_set = crl_set;
  return true;
}

// static
bool CRLSetStorage::ApplyDelta(const CRLSet* in_crl_set,
                               const base::StringPiece& delta_bytes,
                               scoped_refptr<CRLSet>* out_crl_set) {
  base::StringPiece data(delta_bytes);
  std::unique_ptr<base::DictionaryValue> header_dict(ReadHeader(&data));
  if (!header_dict.get())
    return false;

  std::string contents;
  if (!header_dict->GetString("ContentType", &contents))
    return false;
  if (contents != "CRLSetDelta")
    return false;

  int version;
  if (!header_dict->GetInteger("Version", &version) ||
      version != kCurrentFileVersion) {
    return false;
  }

  int sequence, delta_from;
  if (!header_dict->GetInteger("Sequence", &sequence) ||
      !header_dict->GetInteger("DeltaFrom", &delta_from) || delta_from < 0 ||
      static_cast<uint32_t>(delta_from) != in_crl_set->sequence_) {
    return false;
  }

  double not_after;
  if (!header_dict->GetDouble("NotAfter", &not_after)) {
    // NotAfter is optional for now.
    not_after = 0;
  }
  if (not_after < 0)
    return false;

  scoped_refptr<CRLSet> crl_set(new CRLSet);
  crl_set->sequence_ = static_cast<uint32_t>(sequence);
  crl_set->not_after_ = static_cast<uint64_t>(not_after);

  if (!CopyBlockedSPKIsFromHeader(crl_set.get(), header_dict.get()))
    return false;

  std::vector<uint8_t> crl_changes;

  if (!ReadChanges(&data, &crl_changes))
    return false;

  size_t i = 0, j = 0;
  for (std::vector<uint8_t>::const_iterator k = crl_changes.begin();
       k != crl_changes.end(); ++k) {
    if (*k == SYMBOL_SAME) {
      if (i >= in_crl_set->crls_.size())
        return false;
      crl_set->crls_.push_back(in_crl_set->crls_[i]);
      crl_set->crls_index_by_issuer_[in_crl_set->crls_[i].first] = j;
      i++;
      j++;
    } else if (*k == SYMBOL_INSERT) {
      std::string parent_spki_hash;
      std::vector<std::string> serials;
      if (!ReadCRL(&data, &parent_spki_hash, &serials))
        return false;
      crl_set->crls_.push_back(std::make_pair(parent_spki_hash, serials));
      crl_set->crls_index_by_issuer_[parent_spki_hash] = j;
      j++;
    } else if (*k == SYMBOL_DELETE) {
      if (i >= in_crl_set->crls_.size())
        return false;
      i++;
    } else if (*k == SYMBOL_CHANGED) {
      if (i >= in_crl_set->crls_.size())
        return false;
      std::vector<std::string> serials;
      if (!ReadDeltaCRL(&data, in_crl_set->crls_[i].second, &serials))
        return false;
      crl_set->crls_.push_back(
          std::make_pair(in_crl_set->crls_[i].first, serials));
      crl_set->crls_index_by_issuer_[in_crl_set->crls_[i].first] = j;
      i++;
      j++;
    } else {
      NOTREACHED();
      return false;
    }
  }

  if (!data.empty())
    return false;
  if (i != in_crl_set->crls_.size())
    return false;

  *out_crl_set = crl_set;
  return true;
}

// static
bool CRLSetStorage::GetIsDeltaUpdate(const base::StringPiece& bytes,
                                     bool* is_delta) {
  base::StringPiece data(bytes);
  std::unique_ptr<base::DictionaryValue> header_dict(ReadHeader(&data));
  if (!header_dict.get())
    return false;

  std::string contents;
  if (!header_dict->GetString("ContentType", &contents))
    return false;

  if (contents == "CRLSet") {
    *is_delta = false;
  } else if (contents == "CRLSetDelta") {
    *is_delta = true;
  } else {
    return false;
  }

  return true;
}

// static
std::string CRLSetStorage::Serialize(const CRLSet* crl_set) {
  std::string header = base::StringPrintf(
      "{"
      "\"Version\":0,"
      "\"ContentType\":\"CRLSet\","
      "\"Sequence\":%u,"
      "\"DeltaFrom\":0,"
      "\"NumParents\":%u,"
      "\"BlockedSPKIs\":[",
      static_cast<unsigned>(crl_set->sequence_),
      static_cast<unsigned>(crl_set->crls_.size()));

  for (std::vector<std::string>::const_iterator i =
           crl_set->blocked_spkis_.begin();
       i != crl_set->blocked_spkis_.end(); ++i) {
    std::string spki_hash_base64;
    base::Base64Encode(*i, &spki_hash_base64);

    if (i != crl_set->blocked_spkis_.begin())
      header += ",";
    header += "\"" + spki_hash_base64 + "\"";
  }
  header += "]";
  if (crl_set->not_after_ != 0)
    header += base::StringPrintf(",\"NotAfter\":%" PRIu64, crl_set->not_after_);
  header += "}";

  size_t len = 2 /* header len */ + header.size();

  for (CRLSet::CRLList::const_iterator i = crl_set->crls_.begin();
       i != crl_set->crls_.end(); ++i) {
    len += i->first.size() + 4 /* num serials */;
    for (std::vector<std::string>::const_iterator j = i->second.begin();
         j != i->second.end(); ++j) {
      len += 1 /* serial length */ + j->size();
    }
  }

  std::string ret;
  uint8_t* out = reinterpret_cast<uint8_t*>(
      base::WriteInto(&ret, len + 1 /* to include final NUL */));
  size_t off = 0;
  CHECK(base::IsValueInRangeForNumericType<uint16_t>(header.size()));
  out[off++] = static_cast<uint8_t>(header.size());
  out[off++] = static_cast<uint8_t>(header.size() >> 8);
  memcpy(out + off, header.data(), header.size());
  off += header.size();

  for (CRLSet::CRLList::const_iterator i = crl_set->crls_.begin();
       i != crl_set->crls_.end(); ++i) {
    memcpy(out + off, i->first.data(), i->first.size());
    off += i->first.size();
    const uint32_t num_serials = i->second.size();
    memcpy(out + off, &num_serials, sizeof(num_serials));
    off += sizeof(num_serials);

    for (std::vector<std::string>::const_iterator j = i->second.begin();
         j != i->second.end(); ++j) {
      CHECK(base::IsValueInRangeForNumericType<uint8_t>(j->size()));
      out[off++] = static_cast<uint8_t>(j->size());
      memcpy(out + off, j->data(), j->size());
      off += j->size();
    }
  }

  CHECK_EQ(off, len);
  return ret;
}

}  // namespace net
