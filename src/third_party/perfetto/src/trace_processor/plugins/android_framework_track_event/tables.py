# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppInt32
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc
from src.trace_processor.tables.metadata_tables import PROCESS_TABLE

ANDROID_TRACK_EVENT_PROCESS_TABLE = Table(
    python_module=__file__,
    class_name='AndroidTrackEventProcessTable',
    sql_name='__intrinsic_android_track_event_process',
    columns=[
        C('upid', CppTableId(PROCESS_TABLE), cpp_access=CppAccess.READ),
        C('start_seq_id',
          CppOptional(CppInt64()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('package_uid',
          CppOptional(CppInt32()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('caller_uid',
          CppOptional(CppInt32()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('defining_uid',
          CppOptional(CppInt32()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('fw_start_ts',
          CppOptional(CppInt64()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('fw_end_ts',
          CppOptional(CppInt64()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('trigger_type',
          CppOptional(CppString()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('hosting_type',
          CppOptional(CppString()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('hosting_name',
          CppOptional(CppString()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('bind_application_delay_ms',
          CppOptional(CppInt64()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C('process_start_delay_ms',
          CppOptional(CppInt64()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
    ],
    tabledoc=TableDoc(
        doc='Per-process lifecycle from Android framework TrackEvents.',
        group='Android',
        columns={
            'upid':
                'The process this row describes.',
            'start_seq_id':
                'start_seq assigned when the process started.',
            'package_uid':
                'The host package uid.',
            'caller_uid':
                'The caller uid.',
            'defining_uid':
                'The defining uid.',
            'fw_start_ts':
                'Timestamp of AndroidProcessStartEvent.',
            'fw_end_ts':
                'Timestamp of AndroidBinderDiedEvent.',
            'trigger_type':
                'TriggerType of the process start (start reason).',
            'hosting_type':
                'HostingTypeId of the process start.',
            'hosting_name':
                'hostingNameStr from the ProcessRecord.',
            'bind_application_delay_ms':
                'Milliseconds to reach bind application.',
            'process_start_delay_ms':
                'Milliseconds to finish starting the process.',
        },
    ),
)

ALL_TABLES = [ANDROID_TRACK_EVENT_PROCESS_TABLE]
