// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_ASYNC_OPERATION_H_
#define BASE_WIN_ASYNC_OPERATION_H_

#include <unknwn.h>
#include <windows.foundation.h>
#include <wrl/async.h>
#include <wrl/client.h>

#include <type_traits>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/threading/thread_checker.h"

namespace base {
namespace win {

// This file provides an implementation of Windows::Foundation::IAsyncOperation.
// Specializations exist for "regular" types and interface types that inherit
// from IUnknown. Both specializations expose a callback() method, which can be
// used to provide the result that will be forwarded to the registered
// completion handler. For regular types it expects an instance of that type,
// and for interface types it expects a corresponding ComPtr. This class is
// thread-affine and all member methods should be called on the same thread that
// constructed the object. In order to offload heavy result computation,
// base's PostTaskAndReplyWithResult() should be used with the ResultCallback
// passed as a reply.
//
// Example usages:
//
// // Regular types
// auto regular_op = WRL::Make<base::win::AsyncOperation<int>>();
// auto cb = regular_op->callback();
// regular_op->put_Completed(...event handler...);
// ...
// // This will invoke the event handler.
// std::move(cb).Run(123);
// ...
// // Results can be queried:
// int results = 0;
// regular_op->GetResults(&results);
// EXPECT_EQ(123, results);
//
// // Interface types
// auto interface_op = WRL::Make<base::win::AsyncOperation<FooBar*>>();
// auto cb = interface_op->callback();
// interface_op->put_Completed(...event handler...);
// ...
// // This will invoke the event handler.
// std::move(cb).Run(WRL::Make<IFooBarImpl>());
// ...
// // Results can be queried:
// WRL::ComPtr<IFooBar> results;
// interface_op->GetResults(&results);
// // |results| points to the provided IFooBarImpl instance.
//
// // Offloading a heavy computation:
// auto my_op = WRL::Make<base::win::AsyncOperation<FooBar*>>();
// base::PostTaskAndReplyWithResult(
//     base::BindOnce(MakeFooBar), my_op->callback());

namespace internal {

// Template tricks needed to dispatch to the correct implementation below.
//
// For all types which are neither InterfaceGroups nor RuntimeClasses, the
// following three typedefs are synonyms for a single C++ type.  But for
// InterfaceGroups and RuntimeClasses, they are different types:
//   LogicalT: The C++ Type for the InterfaceGroup or RuntimeClass, when
//             used as a template parameter.  Eg "RCFoo*"
//   AbiT:     The C++ type for the default interface used to represent the
//             InterfaceGroup or RuntimeClass when passed as a method parameter.
//             Eg "IFoo*"
//   ComplexT: An instantiation of the Internal "AggregateType" template that
//             combines LogicalT with AbiT. Eg "AggregateType<RCFoo*,IFoo*>"
//
// windows.foundation.collections.h defines the following template and
// semantics in Windows::Foundation::Internal:
//
// template <class LogicalType, class AbiType>
// struct AggregateType;
//
//   LogicalType - the Windows Runtime type (eg, runtime class, inteface group,
//                 etc) being provided as an argument to an _impl template, when
//                 that type cannot be represented at the ABI.
//   AbiType     - the type used for marshalling, ie "at the ABI", for the
//                 logical type.
template <typename T>
using ComplexT =
    typename ABI::Windows::Foundation::IAsyncOperation<T>::TResult_complex;

template <typename T>
using AbiT =
    typename ABI::Windows::Foundation::Internal::GetAbiType<ComplexT<T>>::type;

template <typename T>
using LogicalT = typename ABI::Windows::Foundation::Internal::GetLogicalType<
    ComplexT<T>>::type;

template <typename T>
using InterfaceT = std::remove_pointer_t<AbiT<T>>;

// Compile time switch to decide what container to use for the async results for
// |T|. Depends on whether the underlying Abi type is a pointer to IUnknown or
// not. It queries the internals of Windows::Foundation to obtain this
// information.
template <typename T>
using ResultT =
    std::conditional_t<std::is_convertible<AbiT<T>, IUnknown*>::value,
                       Microsoft::WRL::ComPtr<std::remove_pointer_t<AbiT<T>>>,
                       AbiT<T>>;

template <typename T>
using StorageT =
    std::conditional_t<std::is_convertible<AbiT<T>, IUnknown*>::value,
                       Microsoft::WRL::ComPtr<std::remove_pointer_t<AbiT<T>>>,
                       base::Optional<AbiT<T>>>;

template <typename T>
HRESULT CopyStorage(const Microsoft::WRL::ComPtr<T>& storage, T** results) {
  return storage.CopyTo(results);
}

template <typename T>
HRESULT CopyStorage(const base::Optional<T>& storage, T* results) {
  *results = *storage;
  return S_OK;
}

}  // namespace internal

template <class T>
class AsyncOperation
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<
              Microsoft::WRL::WinRt | Microsoft::WRL::InhibitRoOriginateError>,
          ABI::Windows::Foundation::IAsyncOperation<T>> {
 public:
  using StorageT = internal::StorageT<T>;
  using ResultT = internal::ResultT<T>;
  using Handler = ABI::Windows::Foundation::IAsyncOperationCompletedHandler<T>;
  using ResultCallback = base::OnceCallback<void(ResultT)>;

  AsyncOperation() : weak_factory_(this) {
    // Note: This can't be done in the constructor initializer list. This is
    // because it relies on weak_factory_ to be initialized, which needs to be
    // the last class member. Also applies below.
    callback_ =
        base::BindOnce(&AsyncOperation::OnResult, weak_factory_.GetWeakPtr());
  }

  ~AsyncOperation() { DCHECK_CALLED_ON_VALID_THREAD(thread_checker_); }

  // ABI::Windows::Foundation::IAsyncOperation:
  IFACEMETHODIMP put_Completed(Handler* handler) override {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    handler_ = handler;
    return S_OK;
  }
  IFACEMETHODIMP get_Completed(Handler** handler) override {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    return handler_.CopyTo(handler);
  }
  IFACEMETHODIMP GetResults(internal::AbiT<T>* results) override {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    return storage_ ? internal::CopyStorage(storage_, results) : E_PENDING;
  }

  ResultCallback callback() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    DCHECK(!callback_.is_null());
    return std::move(callback_);
  }

 private:
  void InvokeCompletedHandler() {
    handler_->Invoke(this, ABI::Windows::Foundation::AsyncStatus::Completed);
  }

  void OnResult(ResultT result) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    DCHECK(!storage_);
    storage_ = std::move(result);
    InvokeCompletedHandler();
  }

  ResultCallback callback_;
  Microsoft::WRL::ComPtr<Handler> handler_;
  StorageT storage_;

  THREAD_CHECKER(thread_checker_);
  base::WeakPtrFactory<AsyncOperation> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(AsyncOperation);
};

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_ASYNC_OPERATION_H_
