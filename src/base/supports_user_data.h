// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_SUPPORTS_USER_DATA_H_
#define BASE_SUPPORTS_USER_DATA_H_

#include <map>
#include <memory>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/sequence_checker.h"

// TODO(gab): Removing this include causes IWYU failures in other headers,
// remove it in a follow- up CL.
#include "base/threading/thread_checker.h"

namespace base {

// This is a helper for classes that want to allow users to stash random data by
// key. At destruction all the objects will be destructed.
class BASE_EXPORT SupportsUserData {
 public:
  SupportsUserData();

  // Derive from this class and add your own data members to associate extra
  // information with this object. Alternatively, add this as a public base
  // class to any class with a virtual destructor.
  class BASE_EXPORT Data {
   public:
    virtual ~Data() = default;
  };

  // The user data allows the clients to associate data with this object.
  // Multiple user data values can be stored under different keys.
  // This object will TAKE OWNERSHIP of the given data pointer, and will
  // delete the object if it is changed or the object is destroyed.
  // |key| must not be null--that value is too vulnerable for collision.
  Data* GetUserData(const void* key) const;
  void SetUserData(const void* key, std::unique_ptr<Data> data);
  void RemoveUserData(const void* key);

  // SupportsUserData is not thread-safe, and on debug build will assert it is
  // only used on one execution sequence. Calling this method allows the caller
  // to hand the SupportsUserData instance across execution sequences. Use only
  // if you are taking full control of the synchronization of that hand over.
  void DetachFromSequence();

 protected:
  virtual ~SupportsUserData();

 private:
  using DataMap = std::map<const void*, std::unique_ptr<Data>>;

  // Externally-defined data accessible by key.
  DataMap user_data_;
  // Guards usage of |user_data_|
  SequenceChecker sequence_checker_;

  DISALLOW_COPY_AND_ASSIGN(SupportsUserData);
};

// Adapter class that releases a refcounted object when the
// SupportsUserData::Data object is deleted.
template <typename T>
class UserDataAdapter : public base::SupportsUserData::Data {
 public:
  static T* Get(const SupportsUserData* supports_user_data, const void* key) {
    UserDataAdapter* data =
      static_cast<UserDataAdapter*>(supports_user_data->GetUserData(key));
    return data ? static_cast<T*>(data->object_.get()) : NULL;
  }

  UserDataAdapter(T* object) : object_(object) {}
  T* release() { return object_.release(); }

 private:
  scoped_refptr<T> object_;

  DISALLOW_COPY_AND_ASSIGN(UserDataAdapter);
};

}  // namespace base

#endif  // BASE_SUPPORTS_USER_DATA_H_
