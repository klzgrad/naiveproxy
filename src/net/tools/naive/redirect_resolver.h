// Copyright 2019 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_REDIRECT_RESOLVER_H_
#define NET_TOOLS_NAIVE_REDIRECT_RESOLVER_H_

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"

namespace net {

class DatagramServerSocket;
class IOBufferWithSize;

struct Resolution {
  Resolution();
  ~Resolution();

  uint32_t addr;
  std::string name;
  base::TimeTicks time;
  std::map<std::string, std::list<Resolution>::iterator>::iterator by_name;
  std::map<uint32_t, std::list<Resolution>::iterator>::iterator by_addr;
};

class RedirectResolver {
 public:
  RedirectResolver(std::unique_ptr<DatagramServerSocket> socket,
                   const IPAddress& range,
                   size_t prefix);
  ~RedirectResolver();
  RedirectResolver(const RedirectResolver&) = delete;
  RedirectResolver& operator=(const RedirectResolver&) = delete;

  bool IsInResolvedRange(const IPAddress& address) const;
  std::string FindNameByAddress(const IPAddress& address) const;

 private:
  void DoRead();
  void OnRecv(int result);
  void OnSend(int result);
  int HandleReadResult(int result);

  std::unique_ptr<DatagramServerSocket> socket_;
  IPAddress range_;
  size_t prefix_;
  uint32_t offset_;
  scoped_refptr<IOBufferWithSize> buffer_;
  IPEndPoint recv_address_;

  std::map<std::string, std::list<Resolution>::iterator> resolution_by_name_;
  std::map<uint32_t, std::list<Resolution>::iterator> resolution_by_addr_;
  std::list<Resolution> resolutions_;

  base::WeakPtrFactory<RedirectResolver> weak_ptr_factory_{this};
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_REDIRECT_RESOLVER_H_
