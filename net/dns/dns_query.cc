// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_query.h"

#include "base/big_endian.h"
#include "base/memory/ptr_util.h"
#include "base/sys_byteorder.h"
#include "net/base/io_buffer.h"
#include "net/dns/dns_protocol.h"
#include "net/dns/dns_util.h"
#include "net/dns/record_rdata.h"

namespace net {

namespace {

const size_t kHeaderSize = sizeof(dns_protocol::Header);

// Size of the fixed part of an OPT RR:
// https://tools.ietf.org/html/rfc6891#section-6.1.2
static const size_t kOptRRFixedSize = 11;

// https://tools.ietf.org/html/rfc6891#section-6.2.5
// TODO(robpercival): Determine a good value for this programmatically.
const uint16_t kMaxUdpPayloadSize = 4096;

size_t OptRecordSize(const OptRecordRdata* rdata) {
  return rdata == nullptr ? 0 : kOptRRFixedSize + rdata->buf().size();
}

}  // namespace

// DNS query consists of a 12-byte header followed by a question section.
// For details, see RFC 1035 section 4.1.1.  This header template sets RD
// bit, which directs the name server to pursue query recursively, and sets
// the QDCOUNT to 1, meaning the question section has a single entry.
DnsQuery::DnsQuery(uint16_t id,
                   const base::StringPiece& qname,
                   uint16_t qtype,
                   const OptRecordRdata* opt_rdata)
    : qname_size_(qname.size()),
      io_buffer_(new IOBufferWithSize(kHeaderSize + question_size() +
                                      OptRecordSize(opt_rdata))),
      header_(reinterpret_cast<dns_protocol::Header*>(io_buffer_->data())) {
  DCHECK(!DNSDomainToString(qname).empty());
  *header_ = {};
  header_->id = base::HostToNet16(id);
  header_->flags = base::HostToNet16(dns_protocol::kFlagRD);
  header_->qdcount = base::HostToNet16(1);

  // Write question section after the header.
  base::BigEndianWriter writer(io_buffer_->data() + kHeaderSize,
                               io_buffer_->size() - kHeaderSize);
  writer.WriteBytes(qname.data(), qname.size());
  writer.WriteU16(qtype);
  writer.WriteU16(dns_protocol::kClassIN);

  if (opt_rdata != nullptr) {
    header_->arcount = base::HostToNet16(1);
    // Write OPT pseudo-resource record.
    writer.WriteU8(0);                       // empty domain name (root domain)
    writer.WriteU16(OptRecordRdata::kType);  // type
    writer.WriteU16(kMaxUdpPayloadSize);     // class
    // ttl (next 3 fields)
    writer.WriteU8(0);  // rcode does not apply to requests
    writer.WriteU8(0);  // version
    // TODO(robpercival): Set "DNSSEC OK" flag if/when DNSSEC is supported:
    // https://tools.ietf.org/html/rfc3225#section-3
    writer.WriteU16(0);  // flags
    // rdata
    writer.WriteU16(opt_rdata->buf().size());  // rdata length
    writer.WriteBytes(opt_rdata->buf().data(), opt_rdata->buf().size());
  }
}

DnsQuery::~DnsQuery() = default;

std::unique_ptr<DnsQuery> DnsQuery::CloneWithNewId(uint16_t id) const {
  return base::WrapUnique(new DnsQuery(*this, id));
}

uint16_t DnsQuery::id() const {
  return base::NetToHost16(header_->id);
}

base::StringPiece DnsQuery::qname() const {
  return base::StringPiece(io_buffer_->data() + kHeaderSize, qname_size_);
}

uint16_t DnsQuery::qtype() const {
  uint16_t type;
  base::ReadBigEndian<uint16_t>(io_buffer_->data() + kHeaderSize + qname_size_,
                                &type);
  return type;
}

base::StringPiece DnsQuery::question() const {
  return base::StringPiece(io_buffer_->data() + kHeaderSize, question_size());
}

void DnsQuery::set_flags(uint16_t flags) {
  header_->flags = flags;
}

DnsQuery::DnsQuery(const DnsQuery& orig, uint16_t id) {
  qname_size_ = orig.qname_size_;
  io_buffer_ = new IOBufferWithSize(orig.io_buffer()->size());
  memcpy(io_buffer_.get()->data(), orig.io_buffer()->data(),
         io_buffer_.get()->size());
  header_ = reinterpret_cast<dns_protocol::Header*>(io_buffer_->data());
  header_->id = base::HostToNet16(id);
}

}  // namespace net
