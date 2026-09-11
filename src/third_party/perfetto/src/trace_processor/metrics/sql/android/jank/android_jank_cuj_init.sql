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

-- Create a proto using process metadata.
SELECT RUN_METRIC('android/process_metadata.sql');
-- Create the base table (`android_jank_cuj`) containing all completed CUJs
-- found in the trace.
SELECT RUN_METRIC('android/jank/cujs.sql');

-- Create tables to store each CUJs main, render, HWC release,
-- and GPU completion threads.
-- Also stores the (not CUJ-specific) threads of SF: main, render engine,
-- and GPU completion threads.
INCLUDE PERFETTO MODULE android.cujs.threads;

-- Create tables to store the main slices on each of the relevant threads
-- * `Choreographer#doFrame` on the main thread
-- * `DrawFrames on the render` thread
-- * `waiting for HWC release` on the HWC release thread
-- * `Waiting for GPU completion` on the GPU completion thread
-- * `commit` and `composite` on SF main thread.
-- * `REThreaded::drawLayers` on SF RenderEngine thread.
-- Also extracts vsync ids and GPU completion fence ids that allow us to match
-- slices to concrete vsync IDs.
-- Slices and vsyncs are matched between the app and SF processes by looking
-- at the actual frame timeline data.
-- We only store the slices that were produced for the vsyncs within the
-- CUJ markers.
SELECT RUN_METRIC('android/jank/relevant_slices.sql');

-- Computes the boundaries of specific frames and overall CUJ boundaries
-- on specific important threads since each thread will work on a frame at a
-- slightly different time.
-- We also compute the corrected CUJ ts boundaries. This is necessary because
-- the instrumentation logs begin/end CUJ markers *during* the first frame and
-- typically *right at the start* of the last CUJ frame. The ts boundaries in
-- `android_jank_cuj` table are based on these markers so do not actually
-- contain the whole CUJ, but instead overlap with all Choreographer#doFrame
-- slices that belong to a CUJ.
SELECT RUN_METRIC('android/jank/cujs_boundaries.sql');

-- With relevant slices and corrected boundaries we can now estimate the ts
-- boundaries of each frame within the CUJ.
-- We also match with the data from the actual timeline to check which frames
-- missed the deadline and whether this was due to the app or SF.
SELECT RUN_METRIC('android/jank/frames.sql');

-- Creates tables with slices from various relevant threads that are within
-- the CUJ boundaries. Used as data sources for further processing and
-- jank cause analysis of traces.
SELECT RUN_METRIC('android/jank/slices.sql');

-- Creates tables and functions to be used for manual investigations and
-- jank cause analysis of traces.
SELECT RUN_METRIC('android/jank/internal/query_base.sql');
SELECT RUN_METRIC('android/jank/query_functions.sql');

-- Creates a table that matches CUJ counters with the correct CUJs.
-- After the CUJ ends FrameTracker emits counters with the number of total
-- frames, missed frames, longest frame duration, etc.
-- The same numbers are also reported by FrameTracker to statsd.
SELECT RUN_METRIC('android/jank/internal/counters.sql');
