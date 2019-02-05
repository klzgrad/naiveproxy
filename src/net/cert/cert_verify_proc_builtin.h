// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CERT_VERIFY_PROC_BUILTIN_H_
#define NET_CERT_CERT_VERIFY_PROC_BUILTIN_H_

#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"

namespace net {

class CertVerifyProc;

// TODO(crbug.com/649017): This is not how other cert_verify_proc_*.h are
// implemented -- they expose the type in the header. Use a consistent style
// here too.
NET_EXPORT scoped_refptr<CertVerifyProc> CreateCertVerifyProcBuiltin();

}  // namespace net

#endif  // NET_CERT_CERT_VERIFY_PROC_BUILTIN_H_
