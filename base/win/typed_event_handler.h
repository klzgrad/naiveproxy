// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_TYPED_EVENT_HANDLER_H_
#define BASE_WIN_TYPED_EVENT_HANDLER_H_

#include <windows.foundation.collections.h>
#include <wrl/implements.h>

#include <utility>

#include "base/callback.h"

namespace base {
namespace win {

// This file provides an implementation of Windows::Foundation's
// ITypedEventHandler. It serves as a thin wrapper around a RepeatingCallback,
// that forwards the arguments to its |Invoke| method to the callback's |Run|
// method.
template <typename SenderT, typename ArgsT>
class TypedEventHandler
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          ABI::Windows::Foundation::ITypedEventHandler<SenderT, ArgsT>> {
 public:
  using SenderAbiT =
      typename ABI::Windows::Foundation::Internal::GetAbiType<SenderT>::type;
  using ArgsAbiT =
      typename ABI::Windows::Foundation::Internal::GetAbiType<ArgsT>::type;

  using Handler = base::RepeatingCallback<HRESULT(SenderAbiT, ArgsAbiT)>;

  explicit TypedEventHandler(Handler handler) : handler_(std::move(handler)) {}

  // ABI::Windows::Foundation::ITypedEventHandler:
  IFACEMETHODIMP Invoke(SenderAbiT sender, ArgsAbiT args) override {
    return handler_.Run(std::move(sender), std::move(args));
  }

 private:
  Handler handler_;
};

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_TYPED_EVENT_HANDLER_H_
