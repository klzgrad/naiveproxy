// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_test_util.h"

#include "base/big_endian.h"
#include "base/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/single_thread_task_runner.h"
#include "base/sys_byteorder.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/dns/address_sorter.h"
#include "net/dns/dns_query.h"
#include "net/dns/dns_transaction.h"
#include "net/dns/dns_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

const uint8_t kMalformedResponseHeader[] = {
    // Header
    0x00, 0x14,  // Arbitrary ID
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x01,  // 1 question
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs
};

// Create a response containing a valid question (as would normally be validated
// in DnsTransaction) but completely missing a header-declared answer.
std::unique_ptr<DnsResponse> CreateMalformedResponse(std::string hostname,
                                                     uint16_t type) {
  std::string dns_name;
  CHECK(DNSDomainFromDot(hostname, &dns_name));
  DnsQuery query(0x14 /* id */, dns_name, type);

  // Build response to simulate the barebones validation DnsResponse applies to
  // responses received from the network.
  auto buffer = base::MakeRefCounted<IOBufferWithSize>(
      sizeof(kMalformedResponseHeader) + query.question().size());
  memcpy(buffer->data(), kMalformedResponseHeader,
         sizeof(kMalformedResponseHeader));
  memcpy(buffer->data() + sizeof(kMalformedResponseHeader),
         query.question().data(), query.question().size());

  auto response = std::make_unique<DnsResponse>(buffer, buffer->size());
  CHECK(response->InitParseWithoutQuery(buffer->size()));

  return response;
}

class MockAddressSorter : public AddressSorter {
 public:
  ~MockAddressSorter() override = default;
  void Sort(const AddressList& list, CallbackType callback) const override {
    // Do nothing.
    std::move(callback).Run(true, list);
  }
};

DnsResourceRecord BuildAddressRecord(std::string name, const IPAddress& ip) {
  DCHECK(!name.empty());
  DCHECK(ip.IsValid());

  DnsResourceRecord record;
  record.name = std::move(name);
  record.type = ip.IsIPv4() ? dns_protocol::kTypeA : dns_protocol::kTypeAAAA;
  record.klass = dns_protocol::kClassIN;
  record.ttl = base::TimeDelta::FromDays(1).InSeconds();
  record.SetOwnedRdata(net::IPAddressToPackedString(ip));

  return record;
}

DnsResourceRecord BuildCannonnameRecord(std::string name,
                                        std::string cannonname) {
  DCHECK(!name.empty());
  DCHECK(!cannonname.empty());

  DnsResourceRecord record;
  record.name = std::move(name);
  record.type = dns_protocol::kTypeCNAME;
  record.klass = dns_protocol::kClassIN;
  record.ttl = base::TimeDelta::FromDays(1).InSeconds();
  CHECK(DNSDomainFromDot(cannonname, &record.owned_rdata));
  record.rdata = record.owned_rdata;

  return record;
}

// Note: This is not a fully compliant SOA record, merely the bare amount needed
// in DnsRecord::ParseToAddressList() processessing. This record will not pass
// RecordParsed validation.
DnsResourceRecord BuildSoaRecord(std::string name) {
  DCHECK(!name.empty());

  DnsResourceRecord record;
  record.name = std::move(name);
  record.type = dns_protocol::kTypeSOA;
  record.klass = dns_protocol::kClassIN;
  record.ttl = base::TimeDelta::FromDays(1).InSeconds();
  record.SetOwnedRdata("fake_rdata");

  return record;
}

DnsResourceRecord BuildTextRecord(std::string name,
                                  std::vector<std::string> text_strings) {
  DCHECK(!name.empty());
  DCHECK(!text_strings.empty());

  DnsResourceRecord record;
  record.name = std::move(name);
  record.type = dns_protocol::kTypeTXT;
  record.klass = dns_protocol::kClassIN;
  record.ttl = base::TimeDelta::FromDays(1).InSeconds();

  std::string rdata;
  for (std::string text_string : text_strings) {
    DCHECK(!text_string.empty());

    rdata += base::checked_cast<unsigned char>(text_string.size());
    rdata += std::move(text_string);
  }
  record.SetOwnedRdata(std::move(rdata));

  return record;
}

DnsResourceRecord BuildPointerRecord(std::string name,
                                     std::string pointer_name) {
  DCHECK(!name.empty());
  DCHECK(!pointer_name.empty());

  DnsResourceRecord record;
  record.name = std::move(name);
  record.type = dns_protocol::kTypePTR;
  record.klass = dns_protocol::kClassIN;
  record.ttl = base::TimeDelta::FromDays(1).InSeconds();
  CHECK(DNSDomainFromDot(pointer_name, &record.owned_rdata));
  record.rdata = record.owned_rdata;

  return record;
}

DnsResourceRecord BuildServiceRecord(std::string name,
                                     TestServiceRecord service) {
  DCHECK(!name.empty());
  DCHECK(!service.target.empty());

  DnsResourceRecord record;
  record.name = std::move(name);
  record.type = dns_protocol::kTypeSRV;
  record.klass = dns_protocol::kClassIN;
  record.ttl = base::TimeDelta::FromHours(5).InSeconds();

  std::string rdata;
  char num_buffer[2];
  base::WriteBigEndian(num_buffer, service.priority);
  rdata.append(num_buffer, 2);
  base::WriteBigEndian(num_buffer, service.weight);
  rdata.append(num_buffer, 2);
  base::WriteBigEndian(num_buffer, service.port);
  rdata.append(num_buffer, 2);
  std::string dns_name;
  CHECK(DNSDomainFromDot(service.target, &dns_name));
  rdata += dns_name;

  record.SetOwnedRdata(std::move(rdata));

  return record;
}

// A DnsTransaction which uses MockDnsClientRuleList to determine the response.
class MockTransaction : public DnsTransaction,
                        public base::SupportsWeakPtr<MockTransaction> {
 public:
  MockTransaction(const MockDnsClientRuleList& rules,
                  const std::string& hostname,
                  uint16_t qtype,
                  bool secure,
                  URLRequestContext* url_request_context,
                  DnsTransactionFactory::CallbackType callback)
      : result_(MockDnsClientRule::FAIL),
        hostname_(hostname),
        qtype_(qtype),
        callback_(std::move(callback)),
        started_(false),
        delayed_(false) {
    // Find the relevant rule which matches |qtype|, |secure|, prefix of
    // |hostname|, and |url_request_context| (iff the rule context is not
    // null).
    for (size_t i = 0; i < rules.size(); ++i) {
      const std::string& prefix = rules[i].prefix;
      if ((rules[i].qtype == qtype) && (rules[i].secure == secure) &&
          (hostname.size() >= prefix.size()) &&
          (hostname.compare(0, prefix.size(), prefix) == 0) &&
          (!rules[i].context || rules[i].context == url_request_context)) {
        const MockDnsClientRule::Result* result = &rules[i].result;
        result_ = MockDnsClientRule::Result(result->type);
        delayed_ = rules[i].delay;

        // Generate a DnsResponse when not provided with the rule.
        std::vector<DnsResourceRecord> authority_records;
        std::string dns_name;
        CHECK(DNSDomainFromDot(hostname_, &dns_name));
        base::Optional<DnsQuery> query(base::in_place, 22 /* id */, dns_name,
                                       qtype_);
        switch (result->type) {
          case MockDnsClientRule::NODOMAIN:
          case MockDnsClientRule::EMPTY:
            DCHECK(!result->response);  // Not expected to be provided.
            authority_records = {BuildSoaRecord(hostname_)};
            result_.response = std::make_unique<DnsResponse>(
                22 /* id */, false /* is_authoritative */,
                std::vector<DnsResourceRecord>() /* answers */,
                authority_records,
                std::vector<DnsResourceRecord>() /* additional_records */,
                query,
                result->type == MockDnsClientRule::NODOMAIN
                    ? dns_protocol::kRcodeNXDOMAIN
                    : 0);
            break;
          case MockDnsClientRule::FAIL:
          case MockDnsClientRule::TIMEOUT:
            DCHECK(!result->response);  // Not expected to be provided.
            break;
          case MockDnsClientRule::OK:
            if (result->response) {
              // Copy response in case |rules| are destroyed before the
              // transaction completes.
              result_.response = std::make_unique<DnsResponse>(
                  result->response->io_buffer(),
                  result->response->io_buffer_size());
              CHECK(result_.response->InitParseWithoutQuery(
                  result->response->io_buffer_size()));
            } else {
              // Generated response only available for address types.
              DCHECK(qtype_ == dns_protocol::kTypeA ||
                     qtype_ == dns_protocol::kTypeAAAA);
              result_.response = BuildTestDnsResponse(
                  hostname_, qtype_ == dns_protocol::kTypeA
                                 ? IPAddress::IPv4Localhost()
                                 : IPAddress::IPv6Localhost());
            }
            break;
          case MockDnsClientRule::MALFORMED:
            DCHECK(!result->response);  // Not expected to be provided.
            result_.response = CreateMalformedResponse(hostname_, qtype_);
            break;
        }

        break;
      }
    }
  }

  const std::string& GetHostname() const override { return hostname_; }

  uint16_t GetType() const override { return qtype_; }

  void Start() override {
    EXPECT_FALSE(started_);
    started_ = true;
    if (delayed_)
      return;
    // Using WeakPtr to cleanly cancel when transaction is destroyed.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(&MockTransaction::Finish, AsWeakPtr()));
  }

  void FinishDelayedTransaction() {
    EXPECT_TRUE(delayed_);
    delayed_ = false;
    Finish();
  }

  bool delayed() const { return delayed_; }

 private:
  void Finish() {
    switch (result_.type) {
      case MockDnsClientRule::NODOMAIN:
      case MockDnsClientRule::FAIL:
        std::move(callback_).Run(this, ERR_NAME_NOT_RESOLVED,
                                 result_.response.get());
        break;
      case MockDnsClientRule::EMPTY:
      case MockDnsClientRule::OK:
      case MockDnsClientRule::MALFORMED:
        std::move(callback_).Run(this, OK, result_.response.get());
        break;
      case MockDnsClientRule::TIMEOUT:
        std::move(callback_).Run(this, ERR_DNS_TIMED_OUT, nullptr);
        break;
    }
  }

  void SetRequestPriority(RequestPriority priority) override {}

  MockDnsClientRule::Result result_;
  const std::string hostname_;
  const uint16_t qtype_;
  DnsTransactionFactory::CallbackType callback_;
  bool started_;
  bool delayed_;
};

}  // namespace

std::unique_ptr<DnsResponse> BuildTestDnsResponse(std::string name,
                                                  const IPAddress& ip) {
  DCHECK(ip.IsValid());

  std::vector<DnsResourceRecord> answers = {BuildAddressRecord(name, ip)};
  std::string dns_name;
  CHECK(DNSDomainFromDot(name, &dns_name));
  base::Optional<DnsQuery> query(
      base::in_place, 0, dns_name,
      ip.IsIPv4() ? dns_protocol::kTypeA : dns_protocol::kTypeAAAA);
  return std::make_unique<DnsResponse>(
      0, false, std::move(answers),
      std::vector<DnsResourceRecord>() /* authority_records */,
      std::vector<DnsResourceRecord>() /* additional_records */, query);
}

std::unique_ptr<DnsResponse> BuildTestDnsResponseWithCname(
    std::string name,
    const IPAddress& ip,
    std::string cannonname) {
  DCHECK(ip.IsValid());
  DCHECK(!cannonname.empty());

  std::vector<DnsResourceRecord> answers = {
      BuildCannonnameRecord(name, cannonname),
      BuildAddressRecord(cannonname, ip)};
  std::string dns_name;
  CHECK(DNSDomainFromDot(name, &dns_name));
  base::Optional<DnsQuery> query(
      base::in_place, 0, dns_name,
      ip.IsIPv4() ? dns_protocol::kTypeA : dns_protocol::kTypeAAAA);
  return std::make_unique<DnsResponse>(
      0, false, std::move(answers),
      std::vector<DnsResourceRecord>() /* authority_records */,
      std::vector<DnsResourceRecord>() /* additional_records */, query);
}

std::unique_ptr<DnsResponse> BuildTestDnsTextResponse(
    std::string name,
    std::vector<std::vector<std::string>> text_records,
    std::string answer_name) {
  if (answer_name.empty())
    answer_name = name;

  std::vector<DnsResourceRecord> answers;
  for (std::vector<std::string>& text_record : text_records) {
    answers.push_back(BuildTextRecord(answer_name, std::move(text_record)));
  }

  std::string dns_name;
  CHECK(DNSDomainFromDot(name, &dns_name));
  base::Optional<DnsQuery> query(base::in_place, 0, dns_name,
                                 dns_protocol::kTypeTXT);

  return std::make_unique<DnsResponse>(
      0, false, std::move(answers),
      std::vector<DnsResourceRecord>() /* authority_records */,
      std::vector<DnsResourceRecord>() /* additional_records */, query);
}

std::unique_ptr<DnsResponse> BuildTestDnsPointerResponse(
    std::string name,
    std::vector<std::string> pointer_names,
    std::string answer_name) {
  if (answer_name.empty())
    answer_name = name;

  std::vector<DnsResourceRecord> answers;
  for (std::string& pointer_name : pointer_names) {
    answers.push_back(BuildPointerRecord(answer_name, std::move(pointer_name)));
  }

  std::string dns_name;
  CHECK(DNSDomainFromDot(name, &dns_name));
  base::Optional<DnsQuery> query(base::in_place, 0, dns_name,
                                 dns_protocol::kTypePTR);

  return std::make_unique<DnsResponse>(
      0, false, std::move(answers),
      std::vector<DnsResourceRecord>() /* authority_records */,
      std::vector<DnsResourceRecord>() /* additional_records */, query);
}

std::unique_ptr<DnsResponse> BuildTestDnsServiceResponse(
    std::string name,
    std::vector<TestServiceRecord> service_records,
    std::string answer_name) {
  if (answer_name.empty())
    answer_name = name;

  std::vector<DnsResourceRecord> answers;
  for (TestServiceRecord& service_record : service_records) {
    answers.push_back(
        BuildServiceRecord(answer_name, std::move(service_record)));
  }

  std::string dns_name;
  CHECK(DNSDomainFromDot(name, &dns_name));
  base::Optional<DnsQuery> query(base::in_place, 0, dns_name,
                                 dns_protocol::kTypeSRV);

  return std::make_unique<DnsResponse>(
      0, false, std::move(answers),
      std::vector<DnsResourceRecord>() /* authority_records */,
      std::vector<DnsResourceRecord>() /* additional_records */, query);
}

MockDnsClientRule::Result::Result(ResultType type) : type(type) {}

MockDnsClientRule::Result::Result(std::unique_ptr<DnsResponse> response)
    : type(OK), response(std::move(response)) {}

MockDnsClientRule::Result::Result(Result&& result) = default;

MockDnsClientRule::Result::~Result() = default;

MockDnsClientRule::Result& MockDnsClientRule::Result::operator=(
    Result&& result) = default;

MockDnsClientRule::MockDnsClientRule(const std::string& prefix,
                                     uint16_t qtype,
                                     bool secure,
                                     Result result,
                                     bool delay,
                                     URLRequestContext* context)
    : result(std::move(result)),
      prefix(prefix),
      qtype(qtype),
      secure(secure),
      delay(delay),
      context(context) {}

MockDnsClientRule::MockDnsClientRule(MockDnsClientRule&& rule) = default;

// A DnsTransactionFactory which creates MockTransaction.
class MockDnsClient::MockTransactionFactory : public DnsTransactionFactory {
 public:
  explicit MockTransactionFactory(MockDnsClientRuleList rules)
      : rules_(std::move(rules)) {}

  ~MockTransactionFactory() override = default;

  std::unique_ptr<DnsTransaction> CreateTransaction(
      const std::string& hostname,
      uint16_t qtype,
      DnsTransactionFactory::CallbackType callback,
      const NetLogWithSource&,
      bool secure,
      URLRequestContext* url_request_context) override {
    std::unique_ptr<MockTransaction> transaction =
        std::make_unique<MockTransaction>(rules_, hostname, qtype, secure,
                                          url_request_context,
                                          std::move(callback));
    if (transaction->delayed())
      delayed_transactions_.push_back(transaction->AsWeakPtr());
    return transaction;
  }

  void AddEDNSOption(const OptRecordRdata::Opt& opt) override {}

  void CompleteDelayedTransactions() {
    DelayedTransactionList old_delayed_transactions;
    old_delayed_transactions.swap(delayed_transactions_);
    for (auto it = old_delayed_transactions.begin();
         it != old_delayed_transactions.end(); ++it) {
      if (it->get())
        (*it)->FinishDelayedTransaction();
    }
  }

 private:
  typedef std::vector<base::WeakPtr<MockTransaction>> DelayedTransactionList;

  MockDnsClientRuleList rules_;
  DelayedTransactionList delayed_transactions_;
};

MockDnsClient::MockDnsClient(const DnsConfig& config,
                             MockDnsClientRuleList rules)
    : config_(config),
      factory_(new MockTransactionFactory(std::move(rules))),
      address_sorter_(new MockAddressSorter()) {}

MockDnsClient::~MockDnsClient() = default;

void MockDnsClient::SetConfig(const DnsConfig& config) {
  config_ = config;
}

const DnsConfig* MockDnsClient::GetConfig() const {
  return config_.IsValid() ? &config_ : nullptr;
}

DnsTransactionFactory* MockDnsClient::GetTransactionFactory() {
  return config_.IsValid() ? factory_.get() : nullptr;
}

AddressSorter* MockDnsClient::GetAddressSorter() {
  return address_sorter_.get();
}

void MockDnsClient::CompleteDelayedTransactions() {
  factory_->CompleteDelayedTransactions();
}

}  // namespace net
