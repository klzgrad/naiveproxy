// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SINGLETON_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SINGLETON_H_

#include "net/third_party/quic/platform/impl/quic_singleton_impl.h"

namespace quic {

// Singleton utility. Example usage:
//
// In your header:
//  #include "net/third_party/quic/platform/api/quic_singleton.h"
//  class Foo {
//   public:
//    static Foo* GetInstance();
//    void Bar() { ... }
//   private:
//    Foo() { ... }
//    friend quic::QuicSingletonFriend<Foo>;
//  };
//
// In your source file:
//  Foo* Foo::GetInstance() {
//    return quic::QuicSingleton<Foo>::get();
//  }
//
// To use the singleton:
//  Foo::GetInstance()->Bar();
//
// NOTE: The method accessing Singleton<T>::get() has to be named as GetInstance
// and it is important that Foo::GetInstance() is not inlined in the
// header. This makes sure that when source files from multiple targets include
// this header they don't end up with different copies of the inlined code
// creating multiple copies of the singleton.
template <typename T>
using QuicSingleton = QuicSingletonImpl<T>;

// Type that a class using QuicSingleton must declare as a friend, in order for
// QuicSingleton to be able to access the class's private constructor.
template <typename T>
using QuicSingletonFriend = QuicSingletonFriendImpl<T>;

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SINGLETON_H_
