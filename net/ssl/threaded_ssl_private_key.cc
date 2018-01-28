// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/threaded_ssl_private_key.h"

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/task_runner_util.h"
#include "base/threading/thread_task_runner_handle.h"

namespace net {

namespace {

void DoCallback(const base::WeakPtr<ThreadedSSLPrivateKey>& key,
                const ThreadedSSLPrivateKey::SignCallback& callback,
                std::vector<uint8_t>* signature,
                Error error) {
  if (!key)
    return;
  callback.Run(error, *signature);
}

}  // anonymous namespace

class ThreadedSSLPrivateKey::Core
    : public base::RefCountedThreadSafe<ThreadedSSLPrivateKey::Core> {
 public:
  Core(std::unique_ptr<ThreadedSSLPrivateKey::Delegate> delegate)
      : delegate_(std::move(delegate)) {}

  ThreadedSSLPrivateKey::Delegate* delegate() { return delegate_.get(); }

  Error SignDigest(SSLPrivateKey::Hash hash,
                   const base::StringPiece& input,
                   std::vector<uint8_t>* signature) {
    return delegate_->SignDigest(hash, input, signature);
  }

 private:
  friend class base::RefCountedThreadSafe<Core>;
  ~Core() {}

  std::unique_ptr<ThreadedSSLPrivateKey::Delegate> delegate_;
};

ThreadedSSLPrivateKey::ThreadedSSLPrivateKey(
    std::unique_ptr<ThreadedSSLPrivateKey::Delegate> delegate,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : core_(new Core(std::move(delegate))),
      task_runner_(std::move(task_runner)),
      weak_factory_(this) {}

std::vector<SSLPrivateKey::Hash> ThreadedSSLPrivateKey::GetDigestPreferences() {
  return core_->delegate()->GetDigestPreferences();
}

void ThreadedSSLPrivateKey::SignDigest(
    SSLPrivateKey::Hash hash,
    const base::StringPiece& input,
    const SSLPrivateKey::SignCallback& callback) {
  std::vector<uint8_t>* signature = new std::vector<uint8_t>;
  base::PostTaskAndReplyWithResult(
      task_runner_.get(), FROM_HERE,
      base::Bind(&ThreadedSSLPrivateKey::Core::SignDigest, core_, hash,
                 input.as_string(), base::Unretained(signature)),
      base::Bind(&DoCallback, weak_factory_.GetWeakPtr(), callback,
                 base::Owned(signature)));
}

ThreadedSSLPrivateKey::~ThreadedSSLPrivateKey() {}

}  // namespace net
