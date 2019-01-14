// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/transport_security_persister.h"

#include <memory>
#include <utility>

#include "base/base64.h"
#include "base/bind.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/sequenced_task_runner.h"
#include "base/task_runner_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "crypto/sha2.h"
#include "net/cert/x509_certificate.h"
#include "net/http/transport_security_state.h"

namespace net {

namespace {

std::unique_ptr<base::ListValue> SPKIHashesToListValue(
    const HashValueVector& hashes) {
  auto pins = std::make_unique<base::ListValue>();
  for (size_t i = 0; i != hashes.size(); i++)
    pins->AppendString(hashes[i].ToString());
  return pins;
}

void SPKIHashesFromListValue(const base::ListValue& pins,
                             HashValueVector* hashes) {
  size_t num_pins = pins.GetSize();
  for (size_t i = 0; i < num_pins; ++i) {
    std::string type_and_base64;
    HashValue fingerprint;
    if (pins.GetString(i, &type_and_base64) &&
        fingerprint.FromString(type_and_base64)) {
      hashes->push_back(fingerprint);
    }
  }
}

// This function converts the binary hashes to a base64 string which we can
// include in a JSON file.
std::string HashedDomainToExternalString(const std::string& hashed) {
  std::string out;
  base::Base64Encode(hashed, &out);
  return out;
}

// This inverts |HashedDomainToExternalString|, above. It turns an external
// string (from a JSON file) into an internal (binary) string.
std::string ExternalStringToHashedDomain(const std::string& external) {
  std::string out;
  if (!base::Base64Decode(external, &out) ||
      out.size() != crypto::kSHA256Length) {
    return std::string();
  }

  return out;
}

const char kIncludeSubdomains[] = "include_subdomains";
const char kStsIncludeSubdomains[] = "sts_include_subdomains";
const char kPkpIncludeSubdomains[] = "pkp_include_subdomains";
const char kMode[] = "mode";
const char kExpiry[] = "expiry";
const char kDynamicSPKIHashesExpiry[] = "dynamic_spki_hashes_expiry";
const char kDynamicSPKIHashes[] = "dynamic_spki_hashes";
const char kForceHTTPS[] = "force-https";
const char kStrict[] = "strict";
const char kDefault[] = "default";
const char kPinningOnly[] = "pinning-only";
const char kCreated[] = "created";
const char kStsObserved[] = "sts_observed";
const char kPkpObserved[] = "pkp_observed";
const char kReportUri[] = "report-uri";
// The keys below are contained in a subdictionary keyed as
// |kExpectCTSubdictionary|.
const char kExpectCTSubdictionary[] = "expect_ct";
const char kExpectCTExpiry[] = "expect_ct_expiry";
const char kExpectCTObserved[] = "expect_ct_observed";
const char kExpectCTEnforce[] = "expect_ct_enforce";
const char kExpectCTReportUri[] = "expect_ct_report_uri";

std::string LoadState(const base::FilePath& path) {
  std::string result;
  if (!base::ReadFileToString(path, &result)) {
    return "";
  }
  return result;
}

bool IsDynamicExpectCTEnabled() {
  return base::FeatureList::IsEnabled(
      TransportSecurityState::kDynamicExpectCTFeature);
}

// Populates |host| with default values for the STS and PKP states.
// These default values represent "null" states and are only useful to keep
// the entries in the resulting JSON consistent. The deserializer will ignore
// "null" states.
// TODO(davidben): This can be removed when the STS and PKP states are stored
// independently on disk. https://crbug.com/470295
void PopulateEntryWithDefaults(base::DictionaryValue* host) {
  host->Clear();

  // STS default values.
  host->SetBoolean(kStsIncludeSubdomains, false);
  host->SetDouble(kStsObserved, 0.0);
  host->SetDouble(kExpiry, 0.0);
  host->SetString(kMode, kDefault);

  // PKP default values.
  host->SetBoolean(kPkpIncludeSubdomains, false);
  host->SetDouble(kPkpObserved, 0.0);
  host->SetDouble(kDynamicSPKIHashesExpiry, 0.0);
}

// Serializes STS data from |state| into |toplevel|. Any existing state in
// |toplevel| for each item is overwritten.
void SerializeSTSData(TransportSecurityState* state,
                      base::DictionaryValue* toplevel) {
  TransportSecurityState::STSStateIterator sts_iterator(*state);
  for (; sts_iterator.HasNext(); sts_iterator.Advance()) {
    const std::string& hostname = sts_iterator.hostname();
    const TransportSecurityState::STSState& sts_state =
        sts_iterator.domain_state();

    const std::string key = HashedDomainToExternalString(hostname);
    std::unique_ptr<base::DictionaryValue> serialized(
        new base::DictionaryValue);
    PopulateEntryWithDefaults(serialized.get());

    serialized->SetBoolean(kStsIncludeSubdomains, sts_state.include_subdomains);
    serialized->SetDouble(kStsObserved, sts_state.last_observed.ToDoubleT());
    serialized->SetDouble(kExpiry, sts_state.expiry.ToDoubleT());

    switch (sts_state.upgrade_mode) {
      case TransportSecurityState::STSState::MODE_FORCE_HTTPS:
        serialized->SetString(kMode, kForceHTTPS);
        break;
      case TransportSecurityState::STSState::MODE_DEFAULT:
        serialized->SetString(kMode, kDefault);
        break;
      default:
        NOTREACHED() << "STSState with unknown mode";
        continue;
    }

    toplevel->Set(key, std::move(serialized));
  }
}

// Serializes PKP data from |state| into |toplevel|. For each PKP item in
// |state|, if |toplevel| already contains an item for that hostname, the item
// is updated with the PKP data.
void SerializePKPData(TransportSecurityState* state,
                      base::DictionaryValue* toplevel) {
  base::Time now = base::Time::Now();
  TransportSecurityState::PKPStateIterator pkp_iterator(*state);
  for (; pkp_iterator.HasNext(); pkp_iterator.Advance()) {
    const std::string& hostname = pkp_iterator.hostname();
    const TransportSecurityState::PKPState& pkp_state =
        pkp_iterator.domain_state();

    // See if the current |hostname| already has STS state and, if so, update
    // that entry.
    const std::string key = HashedDomainToExternalString(hostname);
    base::DictionaryValue* serialized = nullptr;
    if (!toplevel->GetDictionary(key, &serialized)) {
      std::unique_ptr<base::DictionaryValue> serialized_scoped(
          new base::DictionaryValue);
      serialized = serialized_scoped.get();
      PopulateEntryWithDefaults(serialized);
      toplevel->Set(key, std::move(serialized_scoped));
    }

    serialized->SetBoolean(kPkpIncludeSubdomains, pkp_state.include_subdomains);
    serialized->SetDouble(kPkpObserved, pkp_state.last_observed.ToDoubleT());
    serialized->SetDouble(kDynamicSPKIHashesExpiry,
                          pkp_state.expiry.ToDoubleT());

    // TODO(svaldez): Historically, both SHA-1 and SHA-256 hashes were
    // accepted in pins. Per spec, only SHA-256 is accepted now, however
    // existing serialized pins are still processed. Migrate historical pins
    // with SHA-1 hashes properly, either by dropping just the bad hashes or
    // the entire pin. See https://crbug.com/448501.
    if (now < pkp_state.expiry) {
      serialized->Set(kDynamicSPKIHashes,
                      SPKIHashesToListValue(pkp_state.spki_hashes));
    }

    serialized->SetString(kReportUri, pkp_state.report_uri.spec());
  }
}

// Serializes Expect-CT data from |state| into |toplevel|. For each Expect-CT
// item in |state|, if |toplevel| already contains an item for that hostname,
// the item is updated to include a subdictionary with key
// |kExpectCTSubdictionary|; otherwise an item is created for that hostname with
// a |kExpectCTSubdictionary| subdictionary.
void SerializeExpectCTData(TransportSecurityState* state,
                           base::DictionaryValue* toplevel) {
  if (!IsDynamicExpectCTEnabled())
    return;
  TransportSecurityState::ExpectCTStateIterator expect_ct_iterator(*state);
  for (; expect_ct_iterator.HasNext(); expect_ct_iterator.Advance()) {
    const std::string& hostname = expect_ct_iterator.hostname();
    const TransportSecurityState::ExpectCTState& expect_ct_state =
        expect_ct_iterator.domain_state();

    // See if the current |hostname| already has STS/PKP state and, if so,
    // update that entry.
    const std::string key = HashedDomainToExternalString(hostname);
    base::DictionaryValue* serialized = nullptr;
    if (!toplevel->GetDictionary(key, &serialized)) {
      std::unique_ptr<base::DictionaryValue> serialized_scoped(
          new base::DictionaryValue);
      serialized = serialized_scoped.get();
      PopulateEntryWithDefaults(serialized);
      toplevel->Set(key, std::move(serialized_scoped));
    }

    std::unique_ptr<base::DictionaryValue> expect_ct_subdictionary(
        new base::DictionaryValue());
    expect_ct_subdictionary->SetDouble(
        kExpectCTObserved, expect_ct_state.last_observed.ToDoubleT());
    expect_ct_subdictionary->SetDouble(kExpectCTExpiry,
                                       expect_ct_state.expiry.ToDoubleT());
    expect_ct_subdictionary->SetBoolean(kExpectCTEnforce,
                                        expect_ct_state.enforce);
    expect_ct_subdictionary->SetString(kExpectCTReportUri,
                                       expect_ct_state.report_uri.spec());
    serialized->Set(kExpectCTSubdictionary, std::move(expect_ct_subdictionary));
  }
}

// Populates |state| with the values in the |kExpectCTSubdictionary|
// subdictionary in |parsed|. Returns false if |parsed| is malformed
// (e.g. missing a required Expect-CT key) and true otherwise. Note that true
// does not necessarily mean that Expect-CT state was present in |parsed|.
bool DeserializeExpectCTState(const base::DictionaryValue* parsed,
                              TransportSecurityState::ExpectCTState* state) {
  const base::DictionaryValue* expect_ct_subdictionary;
  if (!parsed->GetDictionary(kExpectCTSubdictionary,
                             &expect_ct_subdictionary)) {
    // Expect-CT data is not required, so this item is not malformed.
    return true;
  }
  double observed;
  bool has_observed =
      expect_ct_subdictionary->GetDouble(kExpectCTObserved, &observed);
  double expiry;
  bool has_expiry =
      expect_ct_subdictionary->GetDouble(kExpectCTExpiry, &expiry);
  bool enforce;
  bool has_enforce =
      expect_ct_subdictionary->GetBoolean(kExpectCTEnforce, &enforce);
  std::string report_uri_str;
  bool has_report_uri =
      expect_ct_subdictionary->GetString(kExpectCTReportUri, &report_uri_str);

  // If an Expect-CT subdictionary is present, it must have the required keys.
  if (!has_observed || !has_expiry || !has_enforce)
    return false;

  state->last_observed = base::Time::FromDoubleT(observed);
  state->expiry = base::Time::FromDoubleT(expiry);
  state->enforce = enforce;
  if (has_report_uri) {
    GURL report_uri(report_uri_str);
    if (report_uri.is_valid())
      state->report_uri = report_uri;
  }
  return true;
}

}  // namespace

TransportSecurityPersister::TransportSecurityPersister(
    TransportSecurityState* state,
    const base::FilePath& profile_path,
    const scoped_refptr<base::SequencedTaskRunner>& background_runner)
    : transport_security_state_(state),
      writer_(profile_path.AppendASCII("TransportSecurity"), background_runner),
      foreground_runner_(base::ThreadTaskRunnerHandle::Get()),
      background_runner_(background_runner),
      weak_ptr_factory_(this) {
  transport_security_state_->SetDelegate(this);

  base::PostTaskAndReplyWithResult(
      background_runner_.get(), FROM_HERE,
      base::Bind(&LoadState, writer_.path()),
      base::Bind(&TransportSecurityPersister::CompleteLoad,
                 weak_ptr_factory_.GetWeakPtr()));
}

TransportSecurityPersister::~TransportSecurityPersister() {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());

  if (writer_.HasPendingWrite())
    writer_.DoScheduledWrite();

  transport_security_state_->SetDelegate(NULL);
}

void TransportSecurityPersister::StateIsDirty(TransportSecurityState* state) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());
  DCHECK_EQ(transport_security_state_, state);

  writer_.ScheduleWrite(this);
}

bool TransportSecurityPersister::SerializeData(std::string* output) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());

  base::DictionaryValue toplevel;

  // TODO(davidben): Fix the serialization format by splitting the on-disk
  // representation of the STS and PKP states. https://crbug.com/470295.
  SerializeSTSData(transport_security_state_, &toplevel);
  SerializePKPData(transport_security_state_, &toplevel);
  SerializeExpectCTData(transport_security_state_, &toplevel);

  base::JSONWriter::WriteWithOptions(
      toplevel, base::JSONWriter::OPTIONS_PRETTY_PRINT, output);
  return true;
}

bool TransportSecurityPersister::LoadEntries(const std::string& serialized,
                                             bool* dirty) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());

  transport_security_state_->ClearDynamicData();
  return Deserialize(serialized, dirty, transport_security_state_);
}

// static
bool TransportSecurityPersister::Deserialize(const std::string& serialized,
                                             bool* dirty,
                                             TransportSecurityState* state) {
  std::unique_ptr<base::Value> value = base::JSONReader::Read(serialized);
  base::DictionaryValue* dict_value = NULL;
  if (!value.get() || !value->GetAsDictionary(&dict_value))
    return false;

  const base::Time current_time(base::Time::Now());
  bool dirtied = false;

  for (base::DictionaryValue::Iterator i(*dict_value);
       !i.IsAtEnd(); i.Advance()) {
    const base::DictionaryValue* parsed = NULL;
    if (!i.value().GetAsDictionary(&parsed)) {
      LOG(WARNING) << "Could not parse entry " << i.key() << "; skipping entry";
      continue;
    }

    TransportSecurityState::STSState sts_state;
    TransportSecurityState::PKPState pkp_state;
    TransportSecurityState::ExpectCTState expect_ct_state;

    // kIncludeSubdomains is a legacy synonym for kStsIncludeSubdomains and
    // kPkpIncludeSubdomains. Parse at least one of these properties,
    // preferably the new ones.
    bool include_subdomains = false;
    bool parsed_include_subdomains = parsed->GetBoolean(kIncludeSubdomains,
                                                        &include_subdomains);
    sts_state.include_subdomains = include_subdomains;
    pkp_state.include_subdomains = include_subdomains;
    if (parsed->GetBoolean(kStsIncludeSubdomains, &include_subdomains)) {
      sts_state.include_subdomains = include_subdomains;
      parsed_include_subdomains = true;
    }
    if (parsed->GetBoolean(kPkpIncludeSubdomains, &include_subdomains)) {
      pkp_state.include_subdomains = include_subdomains;
      parsed_include_subdomains = true;
    }

    std::string mode_string;
    double expiry = 0;
    if (!parsed_include_subdomains ||
        !parsed->GetString(kMode, &mode_string) ||
        !parsed->GetDouble(kExpiry, &expiry)) {
      LOG(WARNING) << "Could not parse some elements of entry " << i.key()
                   << "; skipping entry";
      continue;
    }

    // Don't fail if this key is not present.
    double dynamic_spki_hashes_expiry = 0;
    parsed->GetDouble(kDynamicSPKIHashesExpiry,
                      &dynamic_spki_hashes_expiry);

    const base::ListValue* pins_list = NULL;
    if (parsed->GetList(kDynamicSPKIHashes, &pins_list)) {
      SPKIHashesFromListValue(*pins_list, &pkp_state.spki_hashes);
    }

    if (mode_string == kForceHTTPS || mode_string == kStrict) {
      sts_state.upgrade_mode =
          TransportSecurityState::STSState::MODE_FORCE_HTTPS;
    } else if (mode_string == kDefault || mode_string == kPinningOnly) {
      sts_state.upgrade_mode = TransportSecurityState::STSState::MODE_DEFAULT;
    } else {
      LOG(WARNING) << "Unknown TransportSecurityState mode string "
                   << mode_string << " found for entry " << i.key()
                   << "; skipping entry";
      continue;
    }

    sts_state.expiry = base::Time::FromDoubleT(expiry);
    pkp_state.expiry = base::Time::FromDoubleT(dynamic_spki_hashes_expiry);

    // Don't fail if this key is not present.
    std::string report_uri_str;
    parsed->GetString(kReportUri, &report_uri_str);
    GURL report_uri(report_uri_str);
    if (report_uri.is_valid())
      pkp_state.report_uri = report_uri;

    double sts_observed;
    double pkp_observed;
    if (parsed->GetDouble(kStsObserved, &sts_observed)) {
      sts_state.last_observed = base::Time::FromDoubleT(sts_observed);
    } else if (parsed->GetDouble(kCreated, &sts_observed)) {
      // kCreated is a legacy synonym for both kStsObserved and kPkpObserved.
      sts_state.last_observed = base::Time::FromDoubleT(sts_observed);
    } else {
      // We're migrating an old entry with no observation date. Make sure we
      // write the new date back in a reasonable time frame.
      dirtied = true;
      sts_state.last_observed = base::Time::Now();
    }
    if (parsed->GetDouble(kPkpObserved, &pkp_observed)) {
      pkp_state.last_observed = base::Time::FromDoubleT(pkp_observed);
    } else if (parsed->GetDouble(kCreated, &pkp_observed)) {
      pkp_state.last_observed = base::Time::FromDoubleT(pkp_observed);
    } else {
      dirtied = true;
      pkp_state.last_observed = base::Time::Now();
    }

    if (!DeserializeExpectCTState(parsed, &expect_ct_state)) {
      continue;
    }

    bool has_sts =
        sts_state.expiry > current_time && sts_state.ShouldUpgradeToSSL();
    bool has_pkp =
        pkp_state.expiry > current_time && pkp_state.HasPublicKeyPins();
    bool has_expect_ct =
        expect_ct_state.expiry > current_time &&
        (expect_ct_state.enforce || !expect_ct_state.report_uri.is_empty());
    if (!has_sts && !has_pkp && !has_expect_ct) {
      // Make sure we dirty the state if we drop an entry. The entries can only
      // be dropped when all the STS, PKP, and Expect-CT states are expired or
      // invalid.
      dirtied = true;
      continue;
    }

    std::string hashed = ExternalStringToHashedDomain(i.key());
    if (hashed.empty()) {
      dirtied = true;
      continue;
    }

    // Until the on-disk storage is split, there will always be 'null' entries.
    // We only register entries that have actual state.
    if (has_sts)
      state->AddOrUpdateEnabledSTSHosts(hashed, sts_state);
    if (has_pkp)
      state->AddOrUpdateEnabledPKPHosts(hashed, pkp_state);
    if (has_expect_ct)
      state->AddOrUpdateEnabledExpectCTHosts(hashed, expect_ct_state);
  }

  *dirty = dirtied;
  return true;
}

void TransportSecurityPersister::CompleteLoad(const std::string& state) {
  DCHECK(foreground_runner_->RunsTasksInCurrentSequence());

  if (state.empty())
    return;

  bool dirty = false;
  if (!LoadEntries(state, &dirty)) {
    LOG(ERROR) << "Failed to deserialize state: " << state;
    return;
  }
  if (dirty)
    StateIsDirty(transport_security_state_);
}

}  // namespace net
