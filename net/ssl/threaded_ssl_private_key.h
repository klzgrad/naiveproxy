// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_THREADED_SSL_PRIVATE_KEY_H_
#define NET_SSL_THREADED_SSL_PRIVATE_KEY_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_piece.h"
#include "net/ssl/ssl_private_key.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace net {

// An SSLPrivateKey implementation which offloads key operations to a background
// task runner.
class ThreadedSSLPrivateKey : public SSLPrivateKey {
 public:
  // Interface for consumers to implement to perform the actual signing
  // operation.
  class Delegate {
   public:
    Delegate() {}
    virtual ~Delegate() {}

    // Returns the digests that are supported by the key in decreasing
    // preference. This method must be callable on any thread.
    virtual std::vector<SSLPrivateKey::Hash> GetDigestPreferences() = 0;

    // Signs |input| as a digest of type |hash|. On success it returns OK and
    // sets |signature| to the resulting signature. Otherwise it returns a net
    // error code. It will only be called on the task runner passed to the
    // owning ThreadedSSLPrivateKey.
    virtual Error SignDigest(Hash hash,
                             const base::StringPiece& input,
                             std::vector<uint8_t>* signature) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  ThreadedSSLPrivateKey(
      std::unique_ptr<Delegate> delegate,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  // SSLPrivateKey implementation.
  std::vector<SSLPrivateKey::Hash> GetDigestPreferences() override;
  void SignDigest(Hash hash,
                  const base::StringPiece& input,
                  const SignCallback& callback) override;

 private:
  ~ThreadedSSLPrivateKey() override;
  class Core;

  scoped_refptr<Core> core_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  base::WeakPtrFactory<ThreadedSSLPrivateKey> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ThreadedSSLPrivateKey);
};

}  // namespace net

#endif  // NET_SSL_THREADED_SSL_PRIVATE_KEY_H_
