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
"""Contains tables for relevant for TODO."""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppSelfTableId
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc

from src.trace_processor.tables.track_tables import TRACK_TABLE

MEMORY_SNAPSHOT_TABLE = Table(
    python_module=__file__,
    class_name='MemorySnapshotTable',
    sql_name='__intrinsic_memory_snapshot',
    columns=[
        C(
            'timestamp',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('track_id', CppTableId(TRACK_TABLE)),
        C(
            'detail_level',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''''',
        group='Memory Snapshots',
        columns={
            'timestamp': '''''',
            'track_id': '''''',
            'detail_level': ''''''
        },
    ),
)

PROCESS_MEMORY_SNAPSHOT_TABLE = Table(
    python_module=__file__,
    class_name='ProcessMemorySnapshotTable',
    sql_name='__intrinsic_process_memory_snapshot',
    columns=[
        C(
            'snapshot_id',
            CppTableId(MEMORY_SNAPSHOT_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'upid',
            CppUint32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''''',
        group='Memory Snapshots',
        columns={
            'snapshot_id': '''''',
            'upid': ''''''
        },
    ),
)

MEMORY_SNAPSHOT_NODE_TABLE = Table(
    python_module=__file__,
    class_name='MemorySnapshotNodeTable',
    sql_name='__intrinsic_memory_snapshot_node',
    columns=[
        C(
            'process_snapshot_id',
            CppTableId(PROCESS_MEMORY_SNAPSHOT_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('parent_node_id', CppOptional(CppSelfTableId())),
        C(
            'path',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'size',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'effective_size',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''''',
        group='Memory Snapshots',
        columns={
            'process_snapshot_id': '''''',
            'parent_node_id': '''''',
            'path': '''''',
            'size': '''''',
            'effective_size': '''''',
            'arg_set_id': ''''''
        },
    ),
)

MEMORY_SNAPSHOT_EDGE_TABLE = Table(
    python_module=__file__,
    class_name='MemorySnapshotEdgeTable',
    sql_name='__intrinsic_memory_snapshot_edge',
    columns=[
        C(
            'source_node_id',
            CppTableId(MEMORY_SNAPSHOT_NODE_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'target_node_id',
            CppTableId(MEMORY_SNAPSHOT_NODE_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'importance',
            CppUint32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''''',
        group='Memory Snapshots',
        columns={
            'source_node_id': '''''',
            'target_node_id': '''''',
            'importance': ''''''
        },
    ),
)

# Keep this list sorted.
ALL_TABLES = [
    MEMORY_SNAPSHOT_EDGE_TABLE,
    MEMORY_SNAPSHOT_NODE_TABLE,
    MEMORY_SNAPSHOT_TABLE,
    PROCESS_MEMORY_SNAPSHOT_TABLE,
]
