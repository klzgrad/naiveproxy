// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_response.h"

#include <limits>

#include "base/big_endian.h"
#include "base/strings/string_util.h"
#include "base/sys_byteorder.h"
#include "net/base/address_list.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/dns/dns_protocol.h"
#include "net/dns/dns_query.h"
#include "net/dns/dns_util.h"

namespace net {

namespace {

const size_t kHeaderSize = sizeof(dns_protocol::Header);

const uint8_t kRcodeMask = 0xf;

}  // namespace

DnsResourceRecord::DnsResourceRecord() {
}

DnsResourceRecord::~DnsResourceRecord() {
}

DnsRecordParser::DnsRecordParser() : packet_(NULL), length_(0), cur_(0) {
}

DnsRecordParser::DnsRecordParser(const void* packet,
                                 size_t length,
                                 size_t offset)
    : packet_(reinterpret_cast<const char*>(packet)),
      length_(length),
      cur_(packet_ + offset) {
  DCHECK_LE(offset, length);
}

unsigned DnsRecordParser::ReadName(const void* const vpos,
                                   std::string* out) const {
  const char* const pos = reinterpret_cast<const char*>(vpos);
  DCHECK(packet_);
  DCHECK_LE(packet_, pos);
  DCHECK_LE(pos, packet_ + length_);

  const char* p = pos;
  const char* end = packet_ + length_;
  // Count number of seen bytes to detect loops.
  unsigned seen = 0;
  // Remember how many bytes were consumed before first jump.
  unsigned consumed = 0;

  if (pos >= end)
    return 0;

  if (out) {
    out->clear();
    out->reserve(dns_protocol::kMaxNameLength);
  }

  for (;;) {
    // The first two bits of the length give the type of the length. It's
    // either a direct length or a pointer to the remainder of the name.
    switch (*p & dns_protocol::kLabelMask) {
      case dns_protocol::kLabelPointer: {
        if (p + sizeof(uint16_t) > end)
          return 0;
        if (consumed == 0) {
          consumed = p - pos + sizeof(uint16_t);
          if (!out)
            return consumed;  // If name is not stored, that's all we need.
        }
        seen += sizeof(uint16_t);
        // If seen the whole packet, then we must be in a loop.
        if (seen > length_)
          return 0;
        uint16_t offset;
        base::ReadBigEndian<uint16_t>(p, &offset);
        offset &= dns_protocol::kOffsetMask;
        p = packet_ + offset;
        if (p >= end)
          return 0;
        break;
      }
      case dns_protocol::kLabelDirect: {
        uint8_t label_len = *p;
        ++p;
        // Note: root domain (".") is NOT included.
        if (label_len == 0) {
          if (consumed == 0) {
            consumed = p - pos;
          }  // else we set |consumed| before first jump
          return consumed;
        }
        if (p + label_len >= end)
          return 0;  // Truncated or missing label.
        if (out) {
          if (!out->empty())
            out->append(".");
          out->append(p, label_len);
        }
        p += label_len;
        seen += 1 + label_len;
        break;
      }
      default:
        // unhandled label type
        return 0;
    }
  }
}

bool DnsRecordParser::ReadRecord(DnsResourceRecord* out) {
  DCHECK(packet_);
  size_t consumed = ReadName(cur_, &out->name);
  if (!consumed)
    return false;
  base::BigEndianReader reader(cur_ + consumed,
                               packet_ + length_ - (cur_ + consumed));
  uint16_t rdlen;
  if (reader.ReadU16(&out->type) &&
      reader.ReadU16(&out->klass) &&
      reader.ReadU32(&out->ttl) &&
      reader.ReadU16(&rdlen) &&
      reader.ReadPiece(&out->rdata, rdlen)) {
    cur_ = reader.ptr();
    return true;
  }
  return false;
}

bool DnsRecordParser::SkipQuestion() {
  size_t consumed = ReadName(cur_, NULL);
  if (!consumed)
    return false;

  const char* next = cur_ + consumed + 2 * sizeof(uint16_t);  // QTYPE + QCLASS
  if (next > packet_ + length_)
    return false;

  cur_ = next;

  return true;
}

DnsResponse::DnsResponse()
    : io_buffer_(new IOBufferWithSize(dns_protocol::kMaxUDPSize + 1)) {
}

DnsResponse::DnsResponse(size_t length)
    : io_buffer_(new IOBufferWithSize(length)) {
}

DnsResponse::DnsResponse(const void* data,
                         size_t length,
                         size_t answer_offset)
    : io_buffer_(new IOBufferWithSize(length)),
      parser_(io_buffer_->data(), length, answer_offset) {
  DCHECK(data);
  memcpy(io_buffer_->data(), data, length);
}

DnsResponse::~DnsResponse() {
}

bool DnsResponse::InitParse(int nbytes, const DnsQuery& query) {
  DCHECK_GE(nbytes, 0);
  // Response includes query, it should be at least that size.
  if (nbytes < query.io_buffer()->size() || nbytes >= io_buffer_->size())
    return false;

  // Match the query id.
  if (base::NetToHost16(header()->id) != query.id())
    return false;

  // Match question count.
  if (base::NetToHost16(header()->qdcount) != 1)
    return false;

  // Match the question section.
  const base::StringPiece question = query.question();
  if (question !=
      base::StringPiece(io_buffer_->data() + kHeaderSize, question.size())) {
    return false;
  }

  // Construct the parser.
  parser_ = DnsRecordParser(io_buffer_->data(), nbytes,
                            kHeaderSize + question.size());
  return true;
}

bool DnsResponse::InitParseWithoutQuery(int nbytes) {
  DCHECK_GE(nbytes, 0);

  if (nbytes < static_cast<int>(kHeaderSize) || nbytes >= io_buffer_->size())
    return false;

  parser_ = DnsRecordParser(io_buffer_->data(), nbytes, kHeaderSize);

  unsigned qdcount = base::NetToHost16(header()->qdcount);
  for (unsigned i = 0; i < qdcount; ++i) {
    if (!parser_.SkipQuestion()) {
      parser_ = DnsRecordParser();  // Make parser invalid again.
      return false;
    }
  }

  return true;
}

bool DnsResponse::IsValid() const {
  return parser_.IsValid();
}

uint16_t DnsResponse::flags() const {
  DCHECK(parser_.IsValid());
  return base::NetToHost16(header()->flags) & ~(kRcodeMask);
}

uint8_t DnsResponse::rcode() const {
  DCHECK(parser_.IsValid());
  return base::NetToHost16(header()->flags) & kRcodeMask;
}

unsigned DnsResponse::answer_count() const {
  DCHECK(parser_.IsValid());
  return base::NetToHost16(header()->ancount);
}

unsigned DnsResponse::additional_answer_count() const {
  DCHECK(parser_.IsValid());
  return base::NetToHost16(header()->arcount);
}

base::StringPiece DnsResponse::qname() const {
  DCHECK(parser_.IsValid());
  // The response is HEADER QNAME QTYPE QCLASS ANSWER.
  // |parser_| is positioned at the beginning of ANSWER, so the end of QNAME is
  // two uint16_ts before it.
  const size_t qname_size =
      parser_.GetOffset() - 2 * sizeof(uint16_t) - kHeaderSize;
  return base::StringPiece(io_buffer_->data() + kHeaderSize, qname_size);
}

uint16_t DnsResponse::qtype() const {
  DCHECK(parser_.IsValid());
  // QTYPE starts where QNAME ends.
  const size_t type_offset = parser_.GetOffset() - 2 * sizeof(uint16_t);
  uint16_t type;
  base::ReadBigEndian<uint16_t>(io_buffer_->data() + type_offset, &type);
  return type;
}

std::string DnsResponse::GetDottedName() const {
  return DNSDomainToString(qname());
}

DnsRecordParser DnsResponse::Parser() const {
  DCHECK(parser_.IsValid());
  // Return a copy of the parser.
  return parser_;
}

const dns_protocol::Header* DnsResponse::header() const {
  return reinterpret_cast<const dns_protocol::Header*>(io_buffer_->data());
}

DnsResponse::Result DnsResponse::ParseToAddressList(
    AddressList* addr_list,
    base::TimeDelta* ttl) const {
  DCHECK(IsValid());
  // DnsTransaction already verified that |response| matches the issued query.
  // We still need to determine if there is a valid chain of CNAMEs from the
  // query name to the RR owner name.
  // We err on the side of caution with the assumption that if we are too picky,
  // we can always fall back to the system getaddrinfo.

  // Expected owner of record. No trailing dot.
  std::string expected_name = GetDottedName();

  uint16_t expected_type = qtype();
  DCHECK(expected_type == dns_protocol::kTypeA ||
         expected_type == dns_protocol::kTypeAAAA);

  size_t expected_size = (expected_type == dns_protocol::kTypeAAAA)
                             ? IPAddress::kIPv6AddressSize
                             : IPAddress::kIPv4AddressSize;

  uint32_t ttl_sec = std::numeric_limits<uint32_t>::max();
  IPAddressList ip_addresses;
  DnsRecordParser parser = Parser();
  DnsResourceRecord record;
  unsigned ancount = answer_count();
  for (unsigned i = 0; i < ancount; ++i) {
    if (!parser.ReadRecord(&record))
      return DNS_MALFORMED_RESPONSE;

    if (record.type == dns_protocol::kTypeCNAME) {
      // Following the CNAME chain, only if no addresses seen.
      if (!ip_addresses.empty())
        return DNS_CNAME_AFTER_ADDRESS;

      if (!base::EqualsCaseInsensitiveASCII(record.name, expected_name))
        return DNS_NAME_MISMATCH;

      if (record.rdata.size() !=
          parser.ReadName(record.rdata.begin(), &expected_name))
        return DNS_MALFORMED_CNAME;

      ttl_sec = std::min(ttl_sec, record.ttl);
    } else if (record.type == expected_type) {
      if (record.rdata.size() != expected_size)
        return DNS_SIZE_MISMATCH;

      if (!base::EqualsCaseInsensitiveASCII(record.name, expected_name))
        return DNS_NAME_MISMATCH;

      ttl_sec = std::min(ttl_sec, record.ttl);
      ip_addresses.push_back(
          IPAddress(reinterpret_cast<const uint8_t*>(record.rdata.data()),
                    record.rdata.length()));
    }
  }

  // TODO(szym): Extract TTL for NODATA results. http://crbug.com/115051

  // getcanonname in eglibc returns the first owner name of an A or AAAA RR.
  // If the response passed all the checks so far, then |expected_name| is it.
  *addr_list = AddressList::CreateFromIPAddressList(ip_addresses,
                                                    expected_name);
  *ttl = base::TimeDelta::FromSeconds(ttl_sec);
  return DNS_PARSE_OK;
}

}  // namespace net
