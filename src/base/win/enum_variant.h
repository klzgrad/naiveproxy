// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_ENUM_VARIANT_H_
#define BASE_WIN_ENUM_VARIANT_H_

#include <unknwn.h>

#include <memory>
#include <vector>

#include "base/win/iunknown_impl.h"
#include "base/win/scoped_variant.h"

namespace base {
namespace win {

// A simple implementation of IEnumVARIANT.
class BASE_EXPORT EnumVariant
  : public IEnumVARIANT,
    public IUnknownImpl {
 public:
  // The constructor allocates a vector of empty ScopedVariants of size |count|.
  // Use ItemAt to set the value of each item in the array.
  explicit EnumVariant(ULONG count);

  // Returns a mutable pointer to the item at position |index|.
  VARIANT* ItemAt(ULONG index);

  // IUnknown.
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;

  // IEnumVARIANT.
  STDMETHODIMP Next(ULONG requested_count,
                    VARIANT* out_elements,
                    ULONG* out_elements_received) override;
  STDMETHODIMP Skip(ULONG skip_count) override;
  STDMETHODIMP Reset() override;
  STDMETHODIMP Clone(IEnumVARIANT** out_cloned_object) override;

 private:
  ~EnumVariant() override;

  std::vector<ScopedVariant> items_;
  ULONG current_index_;
};

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_ENUM_VARIANT_H_
