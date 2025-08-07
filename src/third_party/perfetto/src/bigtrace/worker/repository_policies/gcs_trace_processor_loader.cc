/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define CPPHTTPLIB_NO_EXCEPTIONS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <json/json.h>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/bigtrace/worker/repository_policies/gcs_trace_processor_loader.h"

namespace perfetto::bigtrace {

namespace {

constexpr char kAuthDomain[] = "http://metadata.google.internal";
constexpr char kAuthPath[] =
    "/computeMetadata/v1/instance/service-accounts/default/token";
constexpr char kGcsDomain[] = "https://storage.googleapis.com";
constexpr char kGcsBucketPath[] = "/download/storage/v1/b/";
constexpr char kGcsParams[] = "?alt=media";

}  // namespace

base::StatusOr<std::unique_ptr<trace_processor::TraceProcessor>>
GcsTraceProcessorLoader::LoadTraceProcessor(const std::string& path) {
  trace_processor::Config config;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);

  // Retrieve access token to use in GET request to GCS
  httplib::Headers auth_headers{{"Metadata-Flavor", "Google"}};
  httplib::Client auth_client(kAuthDomain);

  httplib::Result auth_response = auth_client.Get(kAuthPath, auth_headers);
  std::string json_string = auth_response->body;

  if (auth_response->status != httplib::StatusCode::OK_200) {
    return base::ErrStatus("Failed to get access token: %s",
                           auth_response->body.c_str());
  }

  // Parse access token from response
  Json::Value json_value;
  Json::Reader json_reader;
  bool parsed_successfully = json_reader.parse(json_string, json_value);
  if (!parsed_successfully) {
    return base::ErrStatus("Failed to parse GCS access token");
  }
  std::string access_token = json_value["access_token"].asString();

  // Download trace from GCS
  std::string gcs_path = kGcsBucketPath + path + kGcsParams;
  httplib::Headers gcs_headers{{"Authorization", "Bearer " + access_token}};

  httplib::Client gcs_client(kGcsDomain);
  base::Status response_status;

  httplib::Result trace_response = gcs_client.Get(
      gcs_path, gcs_headers,
      [&](const httplib::Response& response) {
        if (httplib::StatusCode::OK_200 != response.status) {
          response_status = base::ErrStatus("Failed to download trace: %s",
                                            response.reason.c_str());
          return false;
        }
        return true;
      },
      [&](const char* data, size_t data_length) {
        std::unique_ptr<uint8_t[]> buf(new uint8_t[data_length]);
        memcpy(buf.get(), data, data_length);
        auto status = tp->Parse(std::move(buf), data_length);
        return true;
      });

  tp->NotifyEndOfFile();

  RETURN_IF_ERROR(response_status);

  return tp;
}
}  // namespace perfetto::bigtrace
