// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_TRANSACTION_H_
#define NET_DNS_DNS_TRANSACTION_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "net/base/net_export.h"
#include "net/dns/record_rdata.h"

namespace net {

class DnsResponse;
class DnsSession;
class NetLogWithSource;

// DnsTransaction implements a stub DNS resolver as defined in RFC 1034.
// The DnsTransaction takes care of retransmissions, name server fallback (or
// round-robin), suffix search, and simple response validation ("does it match
// the query") to fight poisoning.
//
// Destroying DnsTransaction cancels the underlying network effort.
class NET_EXPORT_PRIVATE DnsTransaction {
 public:
  virtual ~DnsTransaction() {}

  // Returns the original |hostname|.
  virtual const std::string& GetHostname() const = 0;

  // Returns the |qtype|.
  virtual uint16_t GetType() const = 0;

  // Starts the transaction.  Always completes asynchronously.
  virtual void Start() = 0;
};

// Creates DnsTransaction which performs asynchronous DNS search.
// It does NOT perform caching, aggregation or prioritization of transactions.
//
// Destroying the factory does NOT affect any already created DnsTransactions.
class NET_EXPORT_PRIVATE DnsTransactionFactory {
 public:
  // Called with the response or NULL if no matching response was received.
  // Note that the |GetDottedName()| of the response may be different than the
  // original |hostname| as a result of suffix search.
  typedef base::Callback<void(DnsTransaction* transaction,
                              int neterror,
                              const DnsResponse* response)> CallbackType;

  virtual ~DnsTransactionFactory() {}

  // Creates DnsTransaction for the given |hostname| and |qtype| (assuming
  // QCLASS is IN). |hostname| should be in the dotted form. A dot at the end
  // implies the domain name is fully-qualified and will be exempt from suffix
  // search. |hostname| should not be an IP literal.
  //
  // The transaction will run |callback| upon asynchronous completion.
  // The |net_log| is used as the parent log.
  virtual std::unique_ptr<DnsTransaction> CreateTransaction(
      const std::string& hostname,
      uint16_t qtype,
      const CallbackType& callback,
      const NetLogWithSource& net_log) WARN_UNUSED_RESULT = 0;

  // The given EDNS0 option will be included in all DNS queries performed by
  // transactions from this factory.
  virtual void AddEDNSOption(const OptRecordRdata::Opt& opt) = 0;

  // Creates a DnsTransactionFactory which creates DnsTransactionImpl using the
  // |session|.
  static std::unique_ptr<DnsTransactionFactory> CreateFactory(
      DnsSession* session) WARN_UNUSED_RESULT;
};

}  // namespace net

#endif  // NET_DNS_DNS_TRANSACTION_H_
