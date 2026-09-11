/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/android_internal/incident_service.h"

#include <binder/IBinder.h>
#include <binder/IServiceManager.h>
#include <binder/Status.h>
#include <incident/incident_report.h>
#include <stddef.h>
#include <stdint.h>

#include <string>

namespace perfetto {
namespace android_internal {

bool StartIncidentReport(const char* dest_pkg,
                         const char* dest_class,
                         int privacy_policy) {
  if (privacy_policy != INCIDENT_REPORT_PRIVACY_POLICY_AUTOMATIC &&
      privacy_policy != INCIDENT_REPORT_PRIVACY_POLICY_EXPLICIT) {
    return false;
  }

  if (strlen(dest_pkg) == 0 || strlen(dest_class) == 0) {
    return false;
  }

  AIncidentReportArgs* args = AIncidentReportArgs_init();

  AIncidentReportArgs_addSection(args, 3026);  // system_trace only
  AIncidentReportArgs_setPrivacyPolicy(args, privacy_policy);
  AIncidentReportArgs_setReceiverPackage(args, dest_pkg);
  AIncidentReportArgs_setReceiverClass(args, dest_class);

  int err = AIncidentReportArgs_takeReport(args);
  AIncidentReportArgs_delete(args);

  return err == 0;
}

}  // namespace android_internal
}  // namespace perfetto
