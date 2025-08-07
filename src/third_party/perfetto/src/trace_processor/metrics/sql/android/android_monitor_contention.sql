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

INCLUDE PERFETTO MODULE android.monitor_contention;

DROP VIEW IF EXISTS android_monitor_contention_output;
CREATE PERFETTO VIEW android_monitor_contention_output AS
SELECT AndroidMonitorContentionMetric(
  'node', (
    SELECT RepeatedField(
      AndroidMonitorContentionMetric_Node(
        'node_parent_id', parent_id,
        'node_child_id', child_id,
        'node_id', id,
        'ts', ts,
        'dur', dur,
        'monotonic_dur', monotonic_dur,
        'blocking_method', blocking_method,
        'blocked_method', blocked_method,
        'short_blocking_method', short_blocking_method,
        'short_blocked_method', short_blocked_method,
        'blocking_src', blocking_src,
        'blocked_src', blocked_src,
        'waiter_count', waiter_count,
        'blocking_thread_name', blocking_thread_name,
        'blocked_thread_name', blocked_thread_name,
        'blocked_thread_tid', blocked_thread_tid,
        'blocking_thread_tid', blocking_thread_tid,
        'process_name', process_name,
        'pid', pid,
        'is_blocked_thread_main', is_blocked_thread_main,
        'is_blocking_thread_main', is_blocking_thread_main,
        'binder_reply_ts', binder_reply_ts,
        'binder_reply_tid', binder_reply_tid,
        'thread_states', (
          SELECT RepeatedField(
            AndroidMonitorContentionMetric_ThreadStateBreakdown(
              'thread_state', thread_state,
              'thread_state_dur', thread_state_dur,
              'thread_state_count', thread_state_count
            )
          ) FROM android_monitor_contention_chain_thread_state_by_txn t WHERE t.id = android_monitor_contention_chain.id
        ),
        'blocked_functions', (
          SELECT RepeatedField(
            AndroidMonitorContentionMetric_BlockedFunctionBreakdown(
              'blocked_function', blocked_function,
              'blocked_function_dur', blocked_function_dur,
              'blocked_function_count', blocked_function_count
            )
          ) FROM android_monitor_contention_chain_blocked_functions_by_txn b WHERE b.id = android_monitor_contention_chain.id
        )
      )
    )
    FROM android_monitor_contention_chain
  )
);
