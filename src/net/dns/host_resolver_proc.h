// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_HOST_RESOLVER_PROC_H_
#define NET_DNS_HOST_RESOLVER_PROC_H_

#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/address_family.h"
#include "net/base/net_export.h"

namespace net {

class AddressList;

// Interface for a getaddrinfo()-like procedure. This is used by unit-tests
// to control the underlying resolutions in HostResolverImpl. HostResolverProcs
// can be chained together; they fallback to the next procedure in the chain
// by calling ResolveUsingPrevious().
//
// Note that implementations of HostResolverProc *MUST BE THREADSAFE*, since
// the HostResolver implementation using them can be multi-threaded.
class NET_EXPORT HostResolverProc
    : public base::RefCountedThreadSafe<HostResolverProc> {
 public:
  explicit HostResolverProc(HostResolverProc* previous);

  // Resolves |host| to an address list, restricting the results to addresses
  // in |address_family|. If successful returns OK and fills |addrlist| with
  // a list of socket addresses. Otherwise returns a network error code, and
  // fills |os_error| with a more specific error if it was non-NULL.
  virtual int Resolve(const std::string& host,
                      AddressFamily address_family,
                      HostResolverFlags host_resolver_flags,
                      AddressList* addrlist,
                      int* os_error) = 0;

 protected:
  friend class base::RefCountedThreadSafe<HostResolverProc>;

  virtual ~HostResolverProc();

  // Asks the fallback procedure (if set) to do the resolve.
  int ResolveUsingPrevious(const std::string& host,
                           AddressFamily address_family,
                           HostResolverFlags host_resolver_flags,
                           AddressList* addrlist,
                           int* os_error);

 private:
  friend class HostResolverImpl;
  friend class MockHostResolverBase;
  friend class ScopedDefaultHostResolverProc;

  // Sets the previous procedure in the chain.  Aborts if this would result in a
  // cycle.
  void SetPreviousProc(HostResolverProc* proc);

  // Sets the last procedure in the chain, i.e. appends |proc| to the end of the
  // current chain.  Aborts if this would result in a cycle.
  void SetLastProc(HostResolverProc* proc);

  // Returns the last procedure in the chain starting at |proc|.  Will return
  // NULL iff |proc| is NULL.
  static HostResolverProc* GetLastProc(HostResolverProc* proc);

  // Sets the default host resolver procedure that is used by HostResolverImpl.
  // This can be used through ScopedDefaultHostResolverProc to set a catch-all
  // DNS block in unit-tests (individual tests should use MockHostResolver to
  // prevent hitting the network).
  static HostResolverProc* SetDefault(HostResolverProc* proc);
  static HostResolverProc* GetDefault();

  scoped_refptr<HostResolverProc> previous_proc_;
  static HostResolverProc* default_proc_;

  DISALLOW_COPY_AND_ASSIGN(HostResolverProc);
};

// Resolves |host| to an address list, using the system's default host resolver.
// (i.e. this calls out to getaddrinfo()). If successful returns OK and fills
// |addrlist| with a list of socket addresses. Otherwise returns a
// network error code, and fills |os_error| with a more specific error if it
// was non-NULL.
NET_EXPORT_PRIVATE int SystemHostResolverCall(
    const std::string& host,
    AddressFamily address_family,
    HostResolverFlags host_resolver_flags,
    AddressList* addrlist,
    int* os_error);

// Wraps call to SystemHostResolverCall as an instance of HostResolverProc.
class NET_EXPORT_PRIVATE SystemHostResolverProc : public HostResolverProc {
 public:
  SystemHostResolverProc();
  int Resolve(const std::string& hostname,
              AddressFamily address_family,
              HostResolverFlags host_resolver_flags,
              AddressList* addr_list,
              int* os_error) override;

 protected:
  ~SystemHostResolverProc() override;

  DISALLOW_COPY_AND_ASSIGN(SystemHostResolverProc);
};

}  // namespace net

#endif  // NET_DNS_HOST_RESOLVER_PROC_H_
