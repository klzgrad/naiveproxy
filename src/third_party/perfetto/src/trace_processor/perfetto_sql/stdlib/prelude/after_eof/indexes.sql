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

-- It's very typical to filter the flow table on either incoming or outgoing slice ids.
CREATE PERFETTO INDEX flow_in ON flow(slice_in);

CREATE PERFETTO INDEX flow_out ON flow(slice_out);

CREATE PERFETTO INDEX slice_parent_id ON __intrinsic_slice(parent_id);

CREATE PERFETTO INDEX slice_track_id ON __intrinsic_slice(track_id);
