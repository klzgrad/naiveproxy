--
-- Copyright 2020 The Android Open Source Project
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


-- TOP processes that have a RenderThread, sorted by CPU time on RT
DROP VIEW IF EXISTS hwui_processes;
CREATE PERFETTO VIEW hwui_processes AS
SELECT
  process.name AS process_name,
  process.upid AS process_upid,
  CAST(SUM(sched.dur) / 1e6 AS INT64) AS rt_cpu_time_ms,
  thread.utid AS render_thread_id
FROM sched
JOIN thread ON (thread.utid = sched.utid AND thread.name = 'RenderThread')
JOIN process ON (process.upid = thread.upid)
GROUP BY process.name
ORDER BY rt_cpu_time_ms DESC;

DROP VIEW IF EXISTS hwui_draw_frame;
CREATE PERFETTO VIEW hwui_draw_frame AS
SELECT
  count(*) AS draw_frame_count,
  max(dur) AS draw_frame_max,
  min(dur) AS draw_frame_min,
  avg(dur) AS draw_frame_avg,
  thread_track.utid AS render_thread_id
FROM slice
JOIN thread_track ON (thread_track.id = slice.track_id)
WHERE slice.name GLOB 'DrawFrame*' AND slice.dur >= 0
GROUP BY thread_track.utid;

DROP VIEW IF EXISTS hwui_flush_commands;
CREATE PERFETTO VIEW hwui_flush_commands AS
SELECT
  count(*) AS flush_count,
  max(dur) AS flush_max,
  min(dur) AS flush_min,
  avg(dur) AS flush_avg,
  thread_track.utid AS render_thread_id
FROM slice
JOIN thread_track ON (thread_track.id = slice.track_id)
WHERE slice.name = 'flush commands' AND slice.dur >= 0
GROUP BY thread_track.utid;

DROP VIEW IF EXISTS hwui_prepare_tree;
CREATE PERFETTO VIEW hwui_prepare_tree AS
SELECT
  count(*) AS prepare_tree_count,
  max(dur) AS prepare_tree_max,
  min(dur) AS prepare_tree_min,
  avg(dur) AS prepare_tree_avg,
  thread_track.utid AS render_thread_id
FROM slice
JOIN thread_track ON (thread_track.id = slice.track_id)
WHERE slice.name = 'prepareTree' AND slice.dur >= 0
GROUP BY thread_track.utid;

DROP VIEW IF EXISTS hwui_gpu_completion;
CREATE PERFETTO VIEW hwui_gpu_completion AS
SELECT
  count(*) AS gpu_completion_count,
  max(dur) AS gpu_completion_max,
  min(dur) AS gpu_completion_min,
  avg(dur) AS gpu_completion_avg,
  thread.upid AS process_upid
FROM slice
JOIN thread_track ON (thread_track.id = slice.track_id)
JOIN thread ON (thread.name = 'GPU completion' AND thread.utid = thread_track.utid)
WHERE slice.name GLOB 'waiting for GPU completion*' AND slice.dur >= 0
GROUP BY thread_track.utid;

DROP VIEW IF EXISTS hwui_ui_record;
CREATE PERFETTO VIEW hwui_ui_record AS
SELECT
  count(*) AS ui_record_count,
  max(dur) AS ui_record_max,
  min(dur) AS ui_record_min,
  avg(dur) AS ui_record_avg,
  thread.upid AS process_upid
FROM slice
JOIN thread_track ON (thread_track.id = slice.track_id)
JOIN thread ON (thread.name = substr(process.name, -15) AND thread.utid = thread_track.utid)
JOIN process ON (process.upid = thread.upid)
WHERE slice.name = 'Record View#draw()' AND slice.dur >= 0
GROUP BY thread_track.utid;

DROP VIEW IF EXISTS hwui_shader_compile;
CREATE PERFETTO VIEW hwui_shader_compile AS
SELECT
  count(*) AS shader_compile_count,
  sum(dur) AS shader_compile_time,
  avg(dur) AS shader_compile_avg,
  thread_track.utid AS render_thread_id
FROM slice
JOIN thread_track ON (thread_track.id = slice.track_id)
WHERE slice.name = 'shader_compile' AND slice.dur >= 0
GROUP BY thread_track.utid;

DROP VIEW IF EXISTS hwui_cache_hit;
CREATE PERFETTO VIEW hwui_cache_hit AS
SELECT
  count(*) AS cache_hit_count,
  sum(dur) AS cache_hit_time,
  avg(dur) AS cache_hit_avg,
  thread_track.utid AS render_thread_id
FROM slice
JOIN thread_track ON (thread_track.id = slice.track_id)
WHERE slice.name = 'cache_hit' AND slice.dur >= 0
GROUP BY thread_track.utid;

DROP VIEW IF EXISTS hwui_cache_miss;
CREATE PERFETTO VIEW hwui_cache_miss AS
SELECT
  count(*) AS cache_miss_count,
  sum(dur) AS cache_miss_time,
  avg(dur) AS cache_miss_avg,
  thread_track.utid AS render_thread_id
FROM slice
JOIN thread_track ON (thread_track.id = slice.track_id)
WHERE slice.name = 'cache_miss' AND slice.dur >= 0
GROUP BY thread_track.utid;

DROP VIEW IF EXISTS hwui_graphics_cpu_mem;
CREATE PERFETTO VIEW hwui_graphics_cpu_mem AS
SELECT
  max(value) AS graphics_cpu_mem_max,
  min(value) AS graphics_cpu_mem_min,
  avg(value) AS graphics_cpu_mem_avg,
  process_counter_track.upid AS process_upid
FROM counter
JOIN process_counter_track ON (counter.track_id = process_counter_track.id)
WHERE name = 'HWUI CPU Memory' AND counter.value >= 0
GROUP BY process_counter_track.upid;

DROP VIEW IF EXISTS hwui_graphics_gpu_mem;
CREATE PERFETTO VIEW hwui_graphics_gpu_mem AS
SELECT
  max(value) AS graphics_gpu_mem_max,
  min(value) AS graphics_gpu_mem_min,
  avg(value) AS graphics_gpu_mem_avg,
  process_counter_track.upid AS process_upid
FROM counter
JOIN process_counter_track ON (counter.track_id = process_counter_track.id)
WHERE name = 'HWUI Misc Memory' AND counter.value >= 0
GROUP BY process_counter_track.upid;

DROP VIEW IF EXISTS hwui_texture_mem;
CREATE PERFETTO VIEW hwui_texture_mem AS
SELECT
  max(value) AS texture_mem_max,
  min(value) AS texture_mem_min,
  avg(value) AS texture_mem_avg,
  process_counter_track.upid AS process_upid
FROM counter
JOIN process_counter_track ON (counter.track_id = process_counter_track.id)
WHERE name = 'HWUI Texture Memory' AND counter.value >= 0
GROUP BY process_counter_track.upid;

DROP VIEW IF EXISTS hwui_all_mem;
CREATE PERFETTO VIEW hwui_all_mem AS
SELECT
  max(value) AS all_mem_max,
  min(value) AS all_mem_min,
  avg(value) AS all_mem_avg,
  process_counter_track.upid AS process_upid
FROM counter
JOIN process_counter_track ON (counter.track_id = process_counter_track.id)
WHERE name = 'HWUI All Memory' AND counter.value >= 0
GROUP BY process_counter_track.upid;

DROP VIEW IF EXISTS android_hwui_metric_output;
CREATE PERFETTO VIEW android_hwui_metric_output AS
SELECT AndroidHwuiMetric(
  'process_info', (
    SELECT RepeatedField(
      ProcessRenderInfo(
        'process_name', process_name,
        'rt_cpu_time_ms', rt_cpu_time_ms,

        'draw_frame_count', hwui_draw_frame.draw_frame_count,
        'draw_frame_max', hwui_draw_frame.draw_frame_max,
        'draw_frame_min', hwui_draw_frame.draw_frame_min,
        'draw_frame_avg', hwui_draw_frame.draw_frame_avg,

        'flush_count', hwui_flush_commands.flush_count,
        'flush_max', hwui_flush_commands.flush_max,
        'flush_min', hwui_flush_commands.flush_min,
        'flush_avg', hwui_flush_commands.flush_avg,

        'prepare_tree_count', hwui_prepare_tree.prepare_tree_count,
        'prepare_tree_max', hwui_prepare_tree.prepare_tree_max,
        'prepare_tree_min', hwui_prepare_tree.prepare_tree_min,
        'prepare_tree_avg', hwui_prepare_tree.prepare_tree_avg,

        'gpu_completion_count', hwui_gpu_completion.gpu_completion_count,
        'gpu_completion_max', hwui_gpu_completion.gpu_completion_max,
        'gpu_completion_min', hwui_gpu_completion.gpu_completion_min,
        'gpu_completion_avg', hwui_gpu_completion.gpu_completion_avg,

        'ui_record_count', hwui_ui_record.ui_record_count,
        'ui_record_max', hwui_ui_record.ui_record_max,
        'ui_record_min', hwui_ui_record.ui_record_min,
        'ui_record_avg', hwui_ui_record.ui_record_avg,

        'shader_compile_count', hwui_shader_compile.shader_compile_count,
        'shader_compile_time', hwui_shader_compile.shader_compile_time,
        'shader_compile_avg', hwui_shader_compile.shader_compile_avg,

        'cache_hit_count', hwui_cache_hit.cache_hit_count,
        'cache_hit_time', hwui_cache_hit.cache_hit_time,
        'cache_hit_avg', hwui_cache_hit.cache_hit_avg,

        'cache_miss_count', hwui_cache_miss.cache_miss_count,
        'cache_miss_time', hwui_cache_miss.cache_miss_time,
        'cache_miss_avg', hwui_cache_miss.cache_miss_avg,

        'graphics_cpu_mem_max', CAST(hwui_graphics_cpu_mem.graphics_cpu_mem_max AS INT64),
        'graphics_cpu_mem_min', CAST(hwui_graphics_cpu_mem.graphics_cpu_mem_min AS INT64),
        'graphics_cpu_mem_avg', hwui_graphics_cpu_mem.graphics_cpu_mem_avg,

        'graphics_gpu_mem_max', CAST(hwui_graphics_gpu_mem.graphics_gpu_mem_max AS INT64),
        'graphics_gpu_mem_min', CAST(hwui_graphics_gpu_mem.graphics_gpu_mem_min AS INT64),
        'graphics_gpu_mem_avg', hwui_graphics_gpu_mem.graphics_gpu_mem_avg,

        'texture_mem_max', CAST(hwui_texture_mem.texture_mem_max AS INT64),
        'texture_mem_min', CAST(hwui_texture_mem.texture_mem_min AS INT64),
        'texture_mem_avg', hwui_texture_mem.texture_mem_avg,

        'all_mem_max', CAST(hwui_all_mem.all_mem_max AS INT64),
        'all_mem_min', CAST(hwui_all_mem.all_mem_min AS INT64),
        'all_mem_avg', hwui_all_mem.all_mem_avg
      )
    )
    FROM hwui_processes
    LEFT JOIN hwui_draw_frame ON (hwui_draw_frame.render_thread_id = hwui_processes.render_thread_id)
    LEFT JOIN hwui_flush_commands ON (hwui_flush_commands.render_thread_id = hwui_processes.render_thread_id)
    LEFT JOIN hwui_prepare_tree ON (hwui_prepare_tree.render_thread_id = hwui_processes.render_thread_id)
    LEFT JOIN hwui_gpu_completion ON (hwui_gpu_completion.process_upid = hwui_processes.process_upid)
    LEFT JOIN hwui_ui_record ON (hwui_ui_record.process_upid = hwui_processes.process_upid)
    LEFT JOIN hwui_shader_compile ON (hwui_shader_compile.render_thread_id = hwui_processes.render_thread_id)
    LEFT JOIN hwui_cache_hit ON (hwui_cache_hit.render_thread_id = hwui_processes.render_thread_id)
    LEFT JOIN hwui_cache_miss ON (hwui_cache_miss.render_thread_id = hwui_processes.render_thread_id)
    LEFT JOIN hwui_graphics_cpu_mem ON (hwui_graphics_cpu_mem.process_upid = hwui_processes.process_upid)
    LEFT JOIN hwui_graphics_gpu_mem ON (hwui_graphics_gpu_mem.process_upid = hwui_processes.process_upid)
    LEFT JOIN hwui_texture_mem ON (hwui_texture_mem.process_upid = hwui_processes.process_upid)
    LEFT JOIN hwui_all_mem ON (hwui_all_mem.process_upid = hwui_processes.process_upid)
  )
);
