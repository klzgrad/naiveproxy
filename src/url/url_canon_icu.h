// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_CANON_ICU_H_
#define URL_URL_CANON_ICU_H_

// ICU integration functions.

#include "base/compiler_specific.h"
#include "base/component_export.h"
#include "base/memory/raw_ptr.h"
#include "url/url_canon.h"

typedef struct UConverter UConverter;

namespace url {

// An implementation of CharsetConverter that implementations can use to
// interface the canonicalizer with ICU's conversion routines.
class COMPONENT_EXPORT(URL) ICUCharsetConverter : public CharsetConverter {
 public:
  // Constructs a converter using an already-existing ICU character set
  // converter. This converter is NOT owned by this object; the lifetime must
  // be managed by the creator such that it is alive as long as this is.
  ICUCharsetConverter(UConverter* converter);

  ~ICUCharsetConverter() override;

  void ConvertFromUTF16(std::u16string_view input,
                        CanonOutput* output) override;

 private:
  // The ICU converter, not owned by this class.
  raw_ptr<UConverter> converter_;
};

}  // namespace url

#endif  // URL_URL_CANON_ICU_H_
