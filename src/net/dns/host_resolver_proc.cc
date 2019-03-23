// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/host_resolver_proc.h"

#include "build/build_config.h"

#include "base/logging.h"
#include "base/sys_byteorder.h"
#include "base/threading/scoped_blocking_call.h"
#include "net/base/address_list.h"
#include "net/base/net_errors.h"
#include "net/base/sys_addrinfo.h"
#include "net/dns/dns_reloader.h"
#include "net/dns/dns_util.h"

#if defined(OS_OPENBSD)
#define AI_ADDRCONFIG 0
#endif

namespace net {

namespace {

bool IsAllLocalhostOfOneFamily(const struct addrinfo* ai) {
  bool saw_v4_localhost = false;
  bool saw_v6_localhost = false;
  for (; ai != NULL; ai = ai->ai_next) {
    switch (ai->ai_family) {
      case AF_INET: {
        const struct sockaddr_in* addr_in =
            reinterpret_cast<struct sockaddr_in*>(ai->ai_addr);
        if ((base::NetToHost32(addr_in->sin_addr.s_addr) & 0xff000000) ==
            0x7f000000)
          saw_v4_localhost = true;
        else
          return false;
        break;
      }
      case AF_INET6: {
        const struct sockaddr_in6* addr_in6 =
            reinterpret_cast<struct sockaddr_in6*>(ai->ai_addr);
        if (IN6_IS_ADDR_LOOPBACK(&addr_in6->sin6_addr))
          saw_v6_localhost = true;
        else
          return false;
        break;
      }
      default:
        NOTREACHED();
        return false;
    }
  }

  return saw_v4_localhost != saw_v6_localhost;
}

}  // namespace

HostResolverProc* HostResolverProc::default_proc_ = NULL;

HostResolverProc::HostResolverProc(HostResolverProc* previous) {
  SetPreviousProc(previous);

  // Implicitly fall-back to the global default procedure.
  if (!previous)
    SetPreviousProc(default_proc_);
}

HostResolverProc::~HostResolverProc() = default;

int HostResolverProc::ResolveUsingPrevious(
    const std::string& host,
    AddressFamily address_family,
    HostResolverFlags host_resolver_flags,
    AddressList* addrlist,
    int* os_error) {
  if (previous_proc_.get()) {
    return previous_proc_->Resolve(
        host, address_family, host_resolver_flags, addrlist, os_error);
  }

  // Final fallback is the system resolver.
  return SystemHostResolverCall(host, address_family, host_resolver_flags,
                                addrlist, os_error);
}

void HostResolverProc::SetPreviousProc(HostResolverProc* proc) {
  HostResolverProc* current_previous = previous_proc_.get();
  previous_proc_ = NULL;
  // Now that we've guaranteed |this| is the last proc in a chain, we can
  // detect potential cycles using GetLastProc().
  previous_proc_ = (GetLastProc(proc) == this) ? current_previous : proc;
}

void HostResolverProc::SetLastProc(HostResolverProc* proc) {
  GetLastProc(this)->SetPreviousProc(proc);
}

// static
HostResolverProc* HostResolverProc::GetLastProc(HostResolverProc* proc) {
  if (proc == NULL)
    return NULL;
  HostResolverProc* last_proc = proc;
  while (last_proc->previous_proc_.get() != NULL)
    last_proc = last_proc->previous_proc_.get();
  return last_proc;
}

// static
HostResolverProc* HostResolverProc::SetDefault(HostResolverProc* proc) {
  HostResolverProc* old = default_proc_;
  default_proc_ = proc;
  return old;
}

// static
HostResolverProc* HostResolverProc::GetDefault() {
  return default_proc_;
}

int SystemHostResolverCall(const std::string& host,
                           AddressFamily address_family,
                           HostResolverFlags host_resolver_flags,
                           AddressList* addrlist,
                           int* os_error) {
  // |host| should be a valid domain name. HostResolverImpl::Resolve has checks
  // to fail early if this is not the case.
  DCHECK(IsValidDNSDomain(host));

  if (os_error)
    *os_error = 0;

  struct addrinfo* ai = NULL;
  struct addrinfo hints = {0};

  switch (address_family) {
    case ADDRESS_FAMILY_IPV4:
      hints.ai_family = AF_INET;
      break;
    case ADDRESS_FAMILY_IPV6:
      hints.ai_family = AF_INET6;
      break;
    case ADDRESS_FAMILY_UNSPECIFIED:
      hints.ai_family = AF_UNSPEC;
      break;
    default:
      NOTREACHED();
      hints.ai_family = AF_UNSPEC;
  }

#if defined(OS_WIN)
  // DO NOT USE AI_ADDRCONFIG ON WINDOWS.
  //
  // The following comment in <winsock2.h> is the best documentation I found
  // on AI_ADDRCONFIG for Windows:
  //   Flags used in "hints" argument to getaddrinfo()
  //       - AI_ADDRCONFIG is supported starting with Vista
  //       - default is AI_ADDRCONFIG ON whether the flag is set or not
  //         because the performance penalty in not having ADDRCONFIG in
  //         the multi-protocol stack environment is severe;
  //         this defaulting may be disabled by specifying the AI_ALL flag,
  //         in that case AI_ADDRCONFIG must be EXPLICITLY specified to
  //         enable ADDRCONFIG behavior
  //
  // Not only is AI_ADDRCONFIG unnecessary, but it can be harmful.  If the
  // computer is not connected to a network, AI_ADDRCONFIG causes getaddrinfo
  // to fail with WSANO_DATA (11004) for "localhost", probably because of the
  // following note on AI_ADDRCONFIG in the MSDN getaddrinfo page:
  //   The IPv4 or IPv6 loopback address is not considered a valid global
  //   address.
  // See http://crbug.com/5234.
  //
  // OpenBSD does not support it, either.
  hints.ai_flags = 0;
#else
  hints.ai_flags = AI_ADDRCONFIG;
#endif

  // On Linux AI_ADDRCONFIG doesn't consider loopback addreses, even if only
  // loopback addresses are configured. So don't use it when there are only
  // loopback addresses.
  if (host_resolver_flags & HOST_RESOLVER_LOOPBACK_ONLY)
    hints.ai_flags &= ~AI_ADDRCONFIG;

  if (host_resolver_flags & HOST_RESOLVER_CANONNAME)
    hints.ai_flags |= AI_CANONNAME;

  // Restrict result set to only this socket type to avoid duplicates.
  hints.ai_socktype = SOCK_STREAM;

  // This function can block for a long time. Use ScopedBlockingCall to increase
  // the current thread pool's capacity and thus avoid reducing CPU usage by the
  // current process during that time.
  base::ScopedBlockingCall scoped_blocking_call(base::BlockingType::WILL_BLOCK);

#if defined(OS_POSIX) && !defined(OS_MACOSX) && !defined(OS_OPENBSD) && \
    !defined(OS_ANDROID) && !defined(OS_FUCHSIA)
  DnsReloaderMaybeReload();
#endif
  int err = getaddrinfo(host.c_str(), NULL, &hints, &ai);
  bool should_retry = false;
  // If the lookup was restricted (either by address family, or address
  // detection), and the results where all localhost of a single family,
  // maybe we should retry.  There were several bugs related to these
  // issues, for example http://crbug.com/42058 and http://crbug.com/49024
  if ((hints.ai_family != AF_UNSPEC || hints.ai_flags & AI_ADDRCONFIG) &&
      err == 0 && IsAllLocalhostOfOneFamily(ai)) {
    if (host_resolver_flags & HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6) {
      hints.ai_family = AF_UNSPEC;
      should_retry = true;
    }
    if (hints.ai_flags & AI_ADDRCONFIG) {
      hints.ai_flags &= ~AI_ADDRCONFIG;
      should_retry = true;
    }
  }
  if (should_retry) {
    if (ai != NULL) {
      freeaddrinfo(ai);
      ai = NULL;
    }
    err = getaddrinfo(host.c_str(), NULL, &hints, &ai);
  }

  if (err) {
#if defined(OS_WIN)
    err = WSAGetLastError();
#endif

    // Return the OS error to the caller.
    if (os_error)
      *os_error = err;

    // If the call to getaddrinfo() failed because of a system error, report
    // it separately from ERR_NAME_NOT_RESOLVED.
#if defined(OS_WIN)
    if (err != WSAHOST_NOT_FOUND && err != WSANO_DATA)
      return ERR_NAME_RESOLUTION_FAILED;
#elif defined(OS_POSIX) && !defined(OS_FREEBSD)
    if (err != EAI_NONAME && err != EAI_NODATA)
      return ERR_NAME_RESOLUTION_FAILED;
#endif

    return ERR_NAME_NOT_RESOLVED;
  }

#if defined(OS_ANDROID)
  // Workaround for Android's getaddrinfo leaving ai==NULL without an error.
  // http://crbug.com/134142
  if (ai == NULL)
    return ERR_NAME_NOT_RESOLVED;
#endif

  *addrlist = AddressList::CreateFromAddrinfo(ai);
  freeaddrinfo(ai);
  return OK;
}

SystemHostResolverProc::SystemHostResolverProc() : HostResolverProc(NULL) {}

int SystemHostResolverProc::Resolve(const std::string& hostname,
                                    AddressFamily address_family,
                                    HostResolverFlags host_resolver_flags,
                                    AddressList* addr_list,
                                    int* os_error) {
  return SystemHostResolverCall(hostname,
                                address_family,
                                host_resolver_flags,
                                addr_list,
                                os_error);
}

SystemHostResolverProc::~SystemHostResolverProc() = default;

}  // namespace net
