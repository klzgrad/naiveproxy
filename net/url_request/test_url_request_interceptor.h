// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_TEST_URL_REQUEST_INTERCEPTOR_H_
#define NET_URL_REQUEST_TEST_URL_REQUEST_INTERCEPTOR_H_

#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"

class GURL;

namespace base {
class FilePath;
class TaskRunner;
}

namespace net {

// Intercepts HTTP requests and gives pre-defined responses to specified URLs.
// The pre-defined responses are loaded from files on disk.  The interception
// occurs while the TestURLRequestInterceptor is alive. This class may be
// instantiated on any thread.
class TestURLRequestInterceptor {
 public:
  // Registers an interceptor for URLs using |scheme| and |hostname|. URLs
  // passed to "SetResponse" are required to use |scheme| and |hostname|.
  // |network_task_runner| is the task runner used for network activity
  // (e.g. where URL requests are processed).
  // |worker_task_runner| will be used to read the files specified by
  // either SetResponse() or SetResponseIgnoreQuery() asynchronously. It
  // must be a task runner allowed to perform disk IO.
  TestURLRequestInterceptor(
      const std::string& scheme,
      const std::string& hostname,
      const scoped_refptr<base::TaskRunner>& network_task_runner,
      const scoped_refptr<base::TaskRunner>& worker_task_runner);
  virtual ~TestURLRequestInterceptor();

  // When requests for |url| arrive, respond with the contents of |path|. The
  // hostname and scheme of |url| must match the corresponding parameters
  // passed as constructor arguments.
  void SetResponse(const GURL& url, const base::FilePath& path);

  // Identical to SetResponse, except that query parameters are ignored on
  // incoming URLs when comparing against |url|.
  void SetResponseIgnoreQuery(const GURL& url, const base::FilePath& path);

  // Returns how many requests have been issued that have a stored reply.
  int GetHitCount();

 private:
  class Delegate;

  const std::string scheme_;
  const std::string hostname_;

  scoped_refptr<base::TaskRunner> network_task_runner_;

  // After creation, |delegate_| lives on the thread of the
  // |network_task_runner_|, and a task to delete it is posted from
  // ~TestURLRequestInterceptor().
  Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(TestURLRequestInterceptor);
};

// Specialization of TestURLRequestInterceptor where scheme is "http" and
// hostname is "localhost".
class LocalHostTestURLRequestInterceptor : public TestURLRequestInterceptor {
 public:
  LocalHostTestURLRequestInterceptor(
      const scoped_refptr<base::TaskRunner>& network_task_runner,
      const scoped_refptr<base::TaskRunner>& worker_task_runner);

 private:
  DISALLOW_COPY_AND_ASSIGN(LocalHostTestURLRequestInterceptor);
};

}  // namespace net

#endif  // NET_URL_REQUEST_TEST_URL_REQUEST_INTERCEPTOR_H_
