// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_persister.h"

#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_observer.h"
#include "net/reporting/reporting_policy.h"
#include "net/reporting/reporting_report.h"

namespace net {
namespace {

std::unique_ptr<base::Value> SerializeOrigin(const url::Origin& origin) {
  auto serialized = std::make_unique<base::DictionaryValue>();

  serialized->SetString("scheme", origin.scheme());
  serialized->SetString("host", origin.host());
  serialized->SetInteger("port", origin.port());
  serialized->SetString("suborigin", origin.suborigin());

  return std::move(serialized);
}

bool DeserializeOrigin(const base::DictionaryValue& serialized,
                       url::Origin* origin_out) {
  std::string scheme;
  if (!serialized.GetString("scheme", &scheme))
    return false;

  std::string host;
  if (!serialized.GetString("host", &host))
    return false;

  int port_int;
  if (!serialized.GetInteger("port", &port_int))
    return false;
  uint16_t port = static_cast<uint16_t>(port_int);
  if (port_int != port)
    return false;

  std::string suborigin;
  if (!serialized.GetString("suborigin", &suborigin))
    return false;

  *origin_out = url::Origin::CreateFromNormalizedTupleWithSuborigin(
      scheme, host, port, suborigin);
  return true;
}

class ReportingPersisterImpl : public ReportingPersister {
 public:
  ReportingPersisterImpl(ReportingContext* context) : context_(context) {}

  // ReportingPersister implementation:

  ~ReportingPersisterImpl() override {}

 private:
  std::string SerializeTicks(base::TimeTicks time_ticks) {
    base::Time time = time_ticks - tick_clock()->NowTicks() + clock()->Now();
    return base::Int64ToString(time.ToInternalValue());
  }

  bool DeserializeTicks(const std::string& serialized,
                        base::TimeTicks* time_ticks_out) {
    int64_t internal;
    if (!base::StringToInt64(serialized, &internal))
      return false;

    base::Time time = base::Time::FromInternalValue(internal);
    *time_ticks_out = time - clock()->Now() + tick_clock()->NowTicks();
    return true;
  }

  std::unique_ptr<base::Value> SerializeReport(const ReportingReport& report) {
    auto serialized = std::make_unique<base::DictionaryValue>();

    serialized->SetString("url", report.url.spec());
    serialized->SetString("group", report.group);
    serialized->SetString("type", report.type);
    serialized->Set("body", report.body->CreateDeepCopy());
    serialized->SetString("queued", SerializeTicks(report.queued));
    serialized->SetInteger("attempts", report.attempts);

    return std::move(serialized);
  }

  bool DeserializeReport(const base::DictionaryValue& report) {
    std::string url_string;
    if (!report.GetString("url", &url_string))
      return false;
    GURL url(url_string);
    if (!url.is_valid())
      return false;

    std::string group;
    if (!report.GetString("group", &group))
      return false;

    std::string type;
    if (!report.GetString("type", &type))
      return false;

    const base::Value* body_original;
    if (!report.Get("body", &body_original))
      return false;
    std::unique_ptr<base::Value> body = body_original->CreateDeepCopy();

    std::string queued_string;
    if (!report.GetString("queued", &queued_string))
      return false;
    base::TimeTicks queued;
    if (!DeserializeTicks(queued_string, &queued))
      return false;

    int attempts;
    if (!report.GetInteger("attempts", &attempts))
      return false;
    if (attempts < 0)
      return false;

    cache()->AddReport(url, group, type, std::move(body), queued, attempts);
    return true;
  }

  std::unique_ptr<base::Value> SerializeReports() {
    std::vector<const ReportingReport*> reports;
    cache()->GetReports(&reports);

    auto serialized = std::make_unique<base::ListValue>();
    for (const ReportingReport* report : reports)
      serialized->Append(SerializeReport(*report));

    return std::move(serialized);
  }

  bool DeserializeReports(const base::ListValue& reports) {
    for (size_t i = 0; i < reports.GetSize(); ++i) {
      const base::DictionaryValue* report;
      if (!reports.GetDictionary(i, &report))
        return false;
      if (!DeserializeReport(*report))
        return false;
    }

    return true;
  }

  std::unique_ptr<base::Value> SerializeClient(const ReportingClient& client) {
    auto serialized = std::make_unique<base::DictionaryValue>();

    serialized->Set("origin", SerializeOrigin(client.origin));
    serialized->SetString("endpoint", client.endpoint.spec());
    serialized->SetBoolean(
        "subdomains",
        client.subdomains == ReportingClient::Subdomains::INCLUDE);
    serialized->SetString("group", client.group);
    serialized->SetString("expires", SerializeTicks(client.expires));

    return std::move(serialized);
  }

  bool DeserializeClient(const base::DictionaryValue& client) {
    const base::DictionaryValue* origin_value;
    if (!client.GetDictionary("origin", &origin_value))
      return false;
    url::Origin origin;
    if (!DeserializeOrigin(*origin_value, &origin))
      return false;

    std::string endpoint_string;
    if (!client.GetString("endpoint", &endpoint_string))
      return false;
    GURL endpoint(endpoint_string);
    if (!endpoint.is_valid())
      return false;

    bool subdomains_bool;
    if (!client.GetBoolean("subdomains", &subdomains_bool))
      return false;
    ReportingClient::Subdomains subdomains =
        subdomains_bool ? ReportingClient::Subdomains::INCLUDE
                        : ReportingClient::Subdomains::EXCLUDE;

    std::string group;
    if (!client.GetString("group", &group))
      return false;

    std::string expires_string;
    if (!client.GetString("expires", &expires_string))
      return false;
    base::TimeTicks expires;
    if (!DeserializeTicks(expires_string, &expires))
      return false;

    cache()->SetClient(origin, endpoint, subdomains, group, expires);
    return true;
  }

  std::unique_ptr<base::Value> SerializeClients() {
    std::vector<const ReportingClient*> clients;
    cache()->GetClients(&clients);

    auto serialized = std::make_unique<base::ListValue>();
    for (const ReportingClient* client : clients)
      serialized->Append(SerializeClient(*client));

    return std::move(serialized);
  }

  bool DeserializeClients(const base::ListValue& clients) {
    for (size_t i = 0; i < clients.GetSize(); ++i) {
      const base::DictionaryValue* client;
      if (!clients.GetDictionary(i, &client))
        return false;
      if (!DeserializeClient(*client))
        return false;
    }

    return true;
  }

  static const int kSupportedVersion = 1;

  std::unique_ptr<base::Value> Serialize() {
    auto serialized = std::make_unique<base::DictionaryValue>();

    serialized->SetInteger("reporting_serialized_cache_version",
                           kSupportedVersion);

    bool persist_reports = policy().persist_reports_across_restarts;
    serialized->SetBoolean("includes_reports", persist_reports);
    if (persist_reports)
      serialized->Set("reports", SerializeReports());

    bool persist_clients = policy().persist_clients_across_restarts;
    serialized->SetBoolean("includes_clients", persist_clients);
    if (persist_clients)
      serialized->Set("clients", SerializeClients());

    return std::move(serialized);
  }

  bool Deserialize(const base::Value& serialized_value) {
    std::vector<const ReportingReport*> reports;
    cache()->GetReports(&reports);
    DCHECK(reports.empty());

    std::vector<const ReportingClient*> clients;
    cache()->GetClients(&clients);
    DCHECK(clients.empty());

    int version;

    const base::DictionaryValue* serialized;
    if (!serialized_value.GetAsDictionary(&serialized))
      return false;

    if (!serialized->GetInteger("reporting_serialized_cache_version", &version))
      return false;
    if (version != kSupportedVersion)
      return false;

    bool includes_reports;
    bool includes_clients;
    if (!serialized->GetBoolean("includes_reports", &includes_reports) ||
        !serialized->GetBoolean("includes_clients", &includes_clients)) {
      return false;
    }

    if (includes_reports) {
      const base::ListValue* reports;
      if (!serialized->GetList("reports", &reports))
        return false;
      if (!DeserializeReports(*reports))
        return false;
    }

    if (includes_clients) {
      const base::ListValue* clients;
      if (!serialized->GetList("clients", &clients))
        return false;
      if (!DeserializeClients(*clients))
        return false;
    }

    return true;
  }

  const ReportingPolicy& policy() { return context_->policy(); }
  base::Clock* clock() { return context_->clock(); }
  base::TickClock* tick_clock() { return context_->tick_clock(); }
  ReportingCache* cache() { return context_->cache(); }

  ReportingContext* context_;
};

}  // namespace

// static
std::unique_ptr<ReportingPersister> ReportingPersister::Create(
    ReportingContext* context) {
  return std::make_unique<ReportingPersisterImpl>(context);
}

ReportingPersister::~ReportingPersister() {}

}  // namespace net
