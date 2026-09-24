--
-- Copyright 2025 The Android Open Source Project
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

-- Device specific device curves with 1D dependency (i.e. curve characteristics
-- are dependent only on one CPU policy). See go/wattson for more info. The
-- rows come from the wattson plugin (see
-- src/trace_processor/plugins/wattson/).
CREATE PERFETTO TABLE _device_curves_1d AS
SELECT * FROM __intrinsic_wattson_curves_cpu_1d();
