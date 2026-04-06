--
-- Copyright 2023 The Android Open Source Project
--
-- Licensed under the Apache License, Version 2.0 (the "License");
-- you may not use this file except in compliance with the License.
-- You may obtain a copy of the License at
--
--     https://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
-- See the License for the specific language governing permissions and
-- limitations under the License.
--

CREATE OR REPLACE PERFETTO FUNCTION GET_LATENCY(tag STRING)
RETURNS DOUBLE AS
SELECT dur/1e6 FROM slices WHERE name = $tag ORDER BY dur DESC LIMIT 1;

DROP VIEW IF EXISTS ad_services_metric_output;

CREATE PERFETTO VIEW ad_services_metric_output
AS
SELECT
  AdServicesMetric(
    'ui_metric',
    (
      SELECT
        RepeatedField(
          AdServicesUiMetric(
            'main_activity_creation_latency',
            (GET_LATENCY('AdServicesSettingsMainActivity#OnCreate')),
            'consent_manager_read_latency',
            (GET_LATENCY('ConsentManager#ReadOperation')),
            'consent_manager_write_latency',
            (GET_LATENCY('ConsentManager#WriteOperation')),
            'consent_manager_initialization_latency',
            (GET_LATENCY('ConsentManager#Initialization'))))
    ),
    'app_set_id_metric',
    (
      SELECT
        RepeatedField(
          AdServicesAppSetIdMetric(
            'latency', GET_LATENCY('AdIdCacheEvent')))
    ),
    'ad_id_metric',
    (
      SELECT
        RepeatedField(
          AdServicesAdIdMetric('latency', GET_LATENCY('AppSetIdEvent')))
    ),
    'odp_metric',
    (
      SELECT
        RepeatedField(
          OnDevicePersonalizationMetric(
            'managing_service_initialization_latency',
            (GET_LATENCY('OdpManagingService#Initialization')),
            'service_delegate_execute_flow_latency',
            (GET_LATENCY('OdpManagingServiceDelegate#Execute')),
            'service_delegate_request_surface_package_latency',
            (GET_LATENCY('OdpManagingServiceDelegate#RequestSurfacePackage')),
            'service_delegate_register_web_trigger_latency',
            (GET_LATENCY('OdpManagingServiceDelegate#RegisterWebTrigger'))))
    ));
