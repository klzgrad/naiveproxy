# Copyright (C) 2023 The Android Open Source Project
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
"""Contains tables for relevant for slices."""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import ColumnDoc
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppSelfTableId
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import SqlAccess
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc
from python.generators.trace_processor_table.public import WrappingSqlView

from src.trace_processor.tables.track_tables import TRACK_TABLE

SLICE_TABLE = Table(
    python_module=__file__,
    class_name='SliceTable',
    sql_name='__intrinsic_slice',
    columns=[
        C(
            'ts',
            CppInt64(),
            flags=ColumnFlag.SORTED,
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'dur',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'track_id',
            CppTableId(TRACK_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'category',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'name',
            CppOptional(CppString()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'depth',
            CppUint32(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('stack_id', CppInt64(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C(
            'parent_stack_id',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'parent_id',
            CppOptional(CppSelfTableId()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'thread_ts',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'thread_dur',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'thread_instruction_count',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'thread_instruction_delta',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    wrapping_sql_view=WrappingSqlView('slice'),
    tabledoc=TableDoc(
        doc='''
          Contains slices from userspace which explains what threads were doing
          during the trace.
        ''',
        group='Events',
        columns={
            'ts':
                'The timestamp at the start of the slice (in nanoseconds).',
            'dur':
                'The duration of the slice (in nanoseconds).',
            'track_id':
                'The id of the track this slice is located on.',
            'category':
                '''
                  The "category" of the slice. If this slice originated with
                  track_event, this column contains the category emitted.
                  Otherwise, it is likely to be null (with limited exceptions).
                ''',
            'name':
                '''
                  The name of the slice. The name describes what was happening
                  during the slice.
                ''',
            'depth':
                'The depth of the slice in the current stack of slices.',
            'stack_id':
                '''
                  A unique identifier obtained from the names of all slices
                  in this stack. This is rarely useful and kept around only
                  for legacy reasons.
                ''',
            'parent_stack_id':
                'The stack_id for the parent of this slice. Rarely useful.',
            'parent_id':
                '''
                  The id of the parent (i.e. immediate ancestor) slice for this
                  slice.
                ''',
            'arg_set_id':
                ColumnDoc(
                    'The id of the argument set associated with this slice.',
                    joinable='args.arg_set_id'),
            'thread_ts':
                '''
                  The thread timestamp at the start of the slice. This column
                  will only be populated if thread timestamp collection is
                  enabled with track_event.
                ''',
            'thread_dur':
                ''''
                  The thread time used by this slice. This column will only be
                  populated if thread timestamp collection is enabled with
                  track_event.
                ''',
            'thread_instruction_count':
                '''
                  The value of the CPU instruction counter at the start of the
                  slice. This column will only be populated if thread
                  instruction collection is enabled with track_event.
                ''',
            'thread_instruction_delta':
                '''
                  The change in value of the CPU instruction counter between the
                  start and end of the slice. This column will only be
                  populated if thread instruction collection is enabled with
                  track_event.
                ''',
        }))

EXPERIMENTAL_FLAT_SLICE_TABLE = Table(
    python_module=__file__,
    class_name='ExperimentalFlatSliceTable',
    sql_name='experimental_flat_slice',
    columns=[
        C('ts', CppInt64(), cpp_access=CppAccess.READ),
        C('dur', CppInt64(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C('track_id', CppTableId(TRACK_TABLE)),
        C('category', CppOptional(CppString())),
        C('name', CppOptional(CppString())),
        C('arg_set_id', CppOptional(CppUint32())),
        C('source_id', CppOptional(CppTableId(SLICE_TABLE))),
        C('start_bound', CppInt64(), flags=ColumnFlag.HIDDEN),
        C('end_bound', CppInt64(), flags=ColumnFlag.HIDDEN),
    ],
    tabledoc=TableDoc(
        doc='''
          An experimental table which "flattens" stacks of slices to contain
          only the "deepest" slice at any point in time on each track.
        ''',
        group='Slice',
        columns={
            'ts':
                '''The timestamp at the start of the slice (in nanoseconds).''',
            'dur':
                '''The duration of the slice (in nanoseconds).''',
            'track_id':
                'The id of the track this slice is located on.',
            'category':
                '''
                  The "category" of the slice. If this slice originated with
                  track_event, this column contains the category emitted.
                  Otherwise, it is likely to be null (with limited exceptions).
                ''',
            'name':
                '''
                  The name of the slice. The name describes what was happening
                  during the slice.
                ''',
            'arg_set_id':
                ColumnDoc(
                    'The id of the argument set associated with this slice.',
                    joinable='args.arg_set_id'),
            'source_id':
                'The id of the slice which this row originated from.',
        }))

ANDROID_NETWORK_PACKETS_TABLE = Table(
    python_module=__file__,
    class_name='AndroidNetworkPacketsTable',
    sql_name='__intrinsic_android_network_packets',
    columns=[
        C('id', CppTableId(SLICE_TABLE), flags=ColumnFlag.SORTED),
        C('iface', CppString()),
        C('direction', CppString()),
        C('packet_transport', CppString()),
        C('packet_length', CppInt64()),
        C('packet_count', CppInt64()),
        C('socket_tag', CppUint32()),
        C('socket_tag_str', CppString()),
        C('socket_uid', CppUint32()),
        C('local_port', CppOptional(CppUint32())),
        C('remote_port', CppOptional(CppUint32())),
        C('packet_icmp_type', CppOptional(CppUint32())),
        C('packet_icmp_code', CppOptional(CppUint32())),
        C('packet_tcp_flags', CppOptional(CppUint32())),
        C('packet_tcp_flags_str', CppOptional(CppString())),
    ],
    add_implicit_column=False,
)

# Keep this list sorted.
ALL_TABLES = [
    ANDROID_NETWORK_PACKETS_TABLE,
    EXPERIMENTAL_FLAT_SLICE_TABLE,
    SLICE_TABLE,
]
