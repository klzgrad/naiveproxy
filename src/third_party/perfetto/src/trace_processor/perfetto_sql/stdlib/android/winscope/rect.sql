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

-- Android Winscope rects.
CREATE PERFETTO VIEW android_winscope_rect (
  -- Rect id
  id LONG,
  -- x
  x DOUBLE,
  -- y
  y DOUBLE,
  -- w
  w DOUBLE,
  -- h
  h DOUBLE
) AS
SELECT
  *
FROM __intrinsic_winscope_rect;

-- Android Winscope transforms.
CREATE PERFETTO VIEW android_winscope_transform (
  -- Transform id
  id LONG,
  -- dsdx
  dsdx DOUBLE,
  -- dtdx
  dtdx DOUBLE,
  -- tx
  tx DOUBLE,
  -- dtdy
  dtdy DOUBLE,
  -- dsdy
  dsdy DOUBLE,
  -- ty
  ty DOUBLE
) AS
SELECT
  id,
  dsdx,
  dtdx,
  tx,
  dtdy,
  dsdy,
  ty
FROM __intrinsic_winscope_transform;

-- Android Winscope trace rects.
CREATE PERFETTO VIEW android_winscope_trace_rect (
  -- Trace rect id
  id LONG,
  -- Rect id
  rect_id LONG,
  -- Group id
  group_id LONG,
  -- Depth
  depth LONG,
  -- Is spy rect
  is_spy LONG,
  -- Is visible
  is_visible LONG,
  -- Opacity
  opacity DOUBLE,
  -- Transform id
  transform_id LONG
) AS
SELECT
  *
FROM __intrinsic_winscope_trace_rect;
