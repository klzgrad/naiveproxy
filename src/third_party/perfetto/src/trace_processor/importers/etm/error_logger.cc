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

#include "src/trace_processor/importers/etm/error_logger.h"

#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "src/trace_processor/importers/etm/opencsd.h"

namespace perfetto::trace_processor::etm {
namespace {
constexpr ocsd_err_severity_t kVerbosity = OCSD_ERR_SEV_ERROR;
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#endif

const ocsd_hndl_err_log_t ErrorLogger::RegisterErrorSource(
    const std::string& component_name) {
  ocsd_hndl_err_log_t handle =
      HANDLE_FIRST_REGISTERED_COMPONENT +
      static_cast<ocsd_hndl_err_log_t>(components_.size());
  components_.push_back(component_name);
  return handle;
}

const ocsd_err_severity_t ErrorLogger::GetErrorLogVerbosity() const {
  return kVerbosity;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

void ErrorLogger::LogError(const ocsd_hndl_err_log_t, const ocsdError* error) {
  if (error->getErrorSeverity() < kVerbosity) {
    return;
  }

  last_error_ = *error;
  const uint8_t channel_id = error->getErrorChanID();
  if (!OCSD_IS_VALID_CS_SRC_ID(error->getErrorChanID())) {
    return;
  }

  auto it = last_error_by_channel_id_.Find(channel_id);
  if (!it) {
    last_error_by_channel_id_.Insert(channel_id, *error);
  } else {
    *it = *error;
  }
}

void ErrorLogger::LogMessage(const ocsd_hndl_err_log_t,
                             const ocsd_err_severity_t,
                             const std::string&) {}

ocsdError* ErrorLogger::GetLastError() {
  return last_error_ ? &*last_error_ : nullptr;
}
ocsdError* ErrorLogger::GetLastIDError(const uint8_t chan_id) {
  return last_error_by_channel_id_.Find(chan_id);
}

ocsdMsgLogger* ErrorLogger::getOutputLogger() {
  return nullptr;
}
void ErrorLogger::setOutputLogger(ocsdMsgLogger*) {
  PERFETTO_FATAL("Don't use this");
}

base::Status ErrorLogger::ToError(ocsd_err_t rc) {
  PERFETTO_CHECK(rc != OCSD_OK);

  if (last_error_) {
    return base::Status(ocsdError::getErrorString(*last_error_));
  }
  return base::Status(
      ocsdError::getErrorString(ocsdError(OCSD_ERR_SEV_ERROR, rc)));
}

base::StatusOr<bool> ErrorLogger::ToErrorOrKeepGoing(
    ocsd_datapath_resp_t resp) {
  switch (resp) {
    case OCSD_RESP_ERR_WAIT:
    case OCSD_RESP_ERR_CONT:
      if (last_error_) {
        return base::Status(ocsdError::getErrorString(*last_error_));
      }
      return base::Status(ocsdDataRespStr(resp).getStr());

    case OCSD_RESP_FATAL_NOT_INIT:
    case OCSD_RESP_FATAL_INVALID_OP:
    case OCSD_RESP_FATAL_INVALID_PARAM:
    case OCSD_RESP_FATAL_INVALID_DATA:
    case OCSD_RESP_FATAL_SYS_ERR:
      return base::Status(ocsdDataRespStr(resp).getStr());

    case OCSD_RESP_CONT:
    case OCSD_RESP_WARN_CONT:
      return true;
    case OCSD_RESP_WAIT:
    case OCSD_RESP_WARN_WAIT:
      return false;
  }
  PERFETTO_CHECK(false);  // For GCC.
}

}  // namespace perfetto::trace_processor::etm
