// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_DNS_QUERY_H_
#define NET_DNS_DNS_QUERY_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_piece.h"
#include "net/base/net_export.h"

namespace net {

class OptRecordRdata;

namespace dns_protocol {
struct Header;
}

class IOBufferWithSize;

// Represents on-the-wire DNS query message as an object.
class NET_EXPORT_PRIVATE DnsQuery {
 public:
  // Constructs a query message from |qname| which *MUST* be in a valid
  // DNS name format, and |qtype|. The qclass is set to IN.
  // If opt_rdata is not null, an OPT record will be added to the "Additional"
  // section of the query.
  DnsQuery(uint16_t id,
           const base::StringPiece& qname,
           uint16_t qtype,
           const OptRecordRdata* opt_rdata = nullptr);
  ~DnsQuery();

  // Clones |this| verbatim, with ID field of the header set to |id|.
  std::unique_ptr<DnsQuery> CloneWithNewId(uint16_t id) const;

  // DnsQuery field accessors.
  uint16_t id() const;
  base::StringPiece qname() const;
  uint16_t qtype() const;

  // Returns the Question section of the query.  Used when matching the
  // response.
  base::StringPiece question() const;

  // IOBuffer accessor to be used for writing out the query.
  IOBufferWithSize* io_buffer() const { return io_buffer_.get(); }

  void set_flags(uint16_t flags);

 private:
  DnsQuery(const DnsQuery& orig, uint16_t id);

  // Returns the size of the question section.
  size_t question_size() const {
    // QNAME + QTYPE + QCLASS
    return qname_size_ + sizeof(uint16_t) + sizeof(uint16_t);
  }

  // Size of the DNS name (*NOT* hostname) we are trying to resolve; used
  // to calculate offsets.
  size_t qname_size_;

  // Contains query bytes to be consumed by higher level Write() call.
  scoped_refptr<IOBufferWithSize> io_buffer_;

  // Pointer to the dns header section.
  dns_protocol::Header* header_;

  DISALLOW_COPY_AND_ASSIGN(DnsQuery);
};

}  // namespace net

#endif  // NET_DNS_DNS_QUERY_H_
