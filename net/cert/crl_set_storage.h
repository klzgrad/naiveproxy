// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CRL_SET_STORAGE_H_
#define NET_CERT_CRL_SET_STORAGE_H_

#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_piece.h"
#include "net/base/net_export.h"
#include "net/cert/crl_set.h"

namespace base {
class DictionaryValue;
}

namespace net {

// Static helpers to save and load CRLSet.
class NET_EXPORT CRLSetStorage {
 public:
  // Parse parses the bytes in |data| and, on success, puts a new CRLSet in
  // |out_crl_set| and returns true.
  static bool Parse(base::StringPiece data,
                    scoped_refptr<CRLSet>* out_crl_set);

  // ApplyDelta returns a new CRLSet in |out_crl_set| that is the result of
  // updating |in_crl_set| with the delta information in |delta_bytes|.
  static bool ApplyDelta(const CRLSet* in_crl_set,
                         const base::StringPiece& delta_bytes,
                         scoped_refptr<CRLSet>* out_crl_set);

  // GetIsDeltaUpdate extracts the header from |bytes|, sets *is_delta to
  // whether |bytes| is a delta CRL set or not and returns true. In the event
  // of a parse error, it returns false.
  static bool GetIsDeltaUpdate(const base::StringPiece& bytes, bool *is_delta);

  // Serialize returns a string of bytes suitable for passing to Parse. Parsing
  // and serializing a CRLSet is a lossless operation - the resulting bytes
  // will be equal.
  static std::string Serialize(const CRLSet* crl_set);

 private:
  // CopyBlockedSPKIsFromHeader sets |blocked_spkis_| to the list of values
  // from "BlockedSPKIs" in |header_dict|.
  static bool CopyBlockedSPKIsFromHeader(CRLSet* crl_set,
                                         base::DictionaryValue* header_dict);
};

}  // namespace net

#endif  // NET_CERT_CRL_SET_STORAGE_H_
