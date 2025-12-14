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
"""Contains tables for finding ancestor events."""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppDouble
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table

from src.trace_processor.tables.profiler_tables import STACK_PROFILE_CALLSITE_TABLE
from src.trace_processor.tables.profiler_tables import STACK_PROFILE_FRAME_TABLE
from src.trace_processor.tables.slice_tables import SLICE_TABLE
from src.trace_processor.tables.track_tables import TRACK_TABLE
from src.trace_processor.tables.winscope_tables import SURFACE_FLINGER_LAYERS_SNAPSHOT_TABLE

TABLE_INFO_TABLE = Table(
    python_module=__file__,
    class_name="PerfettoTableInfoTable",
    sql_name="perfetto_table_info",
    columns=[
        C("table_name", CppString(), flags=ColumnFlag.HIDDEN),
        C('name', CppString()),
        C('col_type', CppString()),
        C('nullable', CppInt64()),
        C('sorted', CppInt64()),
    ])

SLICE_SUBSET_TABLE = Table(
    python_module=__file__,
    class_name="SliceSubsetTable",
    sql_name="not_exposed_to_sql",
    columns=[
        C('id', CppTableId(SLICE_TABLE), flags=ColumnFlag.SORTED),
        C('ts', CppInt64(), flags=ColumnFlag.SORTED),
        C('dur', CppInt64()),
        C('track_id', CppTableId(TRACK_TABLE)),
        C('category', CppOptional(CppString())),
        C('name', CppOptional(CppString())),
        C('depth', CppUint32()),
        C('parent_id', CppOptional(CppTableId(SLICE_TABLE))),
        C('arg_set_id', CppOptional(CppUint32())),
        C('thread_ts', CppOptional(CppInt64())),
        C('thread_dur', CppOptional(CppInt64())),
        C('thread_instruction_count', CppOptional(CppInt64())),
        C('thread_instruction_delta', CppOptional(CppInt64())),
    ],
    add_implicit_column=False,
)

ANCESTOR_STACK_PROFILE_CALLSITE_TABLE = Table(
    python_module=__file__,
    class_name="AncestorStackProfileCallsiteTable",
    sql_name="not_exposed_to_sql",
    columns=[
        C(
            'id',
            CppTableId(STACK_PROFILE_CALLSITE_TABLE),
            flags=ColumnFlag.SORTED,
        ),
        C('depth', CppUint32()),
        C('parent_id', CppOptional(CppTableId(STACK_PROFILE_CALLSITE_TABLE))),
        C('frame_id', CppTableId(STACK_PROFILE_FRAME_TABLE)),
    ],
    add_implicit_column=False,
)

CONNECTED_FLOW_TABLE = Table(
    python_module=__file__,
    class_name="ConnectedFlowTable",
    sql_name="not_exposed_to_sql",
    columns=[
        C('slice_out', CppTableId(SLICE_TABLE)),
        C('slice_in', CppTableId(SLICE_TABLE)),
        C('trace_id', CppOptional(CppInt64())),
        C('arg_set_id', CppOptional(CppUint32())),
    ],
)

ARGS_WITH_DEFAULTS_TABLE = Table(
    python_module=__file__,
    class_name='WinscopeArgsWithDefaultsTable',
    sql_name='__intrinsic_winscope_proto_to_args_with_defaults',
    columns=[
        C("table_name", CppString(), flags=ColumnFlag.HIDDEN),
        C(
            'base64_proto_id',
            CppUint32(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'flat_key',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'key',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'int_value',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'string_value',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'real_value',
            CppOptional(CppDouble()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'value_type',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
)

EXPERIMENTAL_ANNOTATED_CALLSTACK_TABLE = Table(
    python_module=__file__,
    class_name="ExperimentalAnnotatedCallstackTable",
    sql_name="experimental_annotated_callstack",
    columns=[
        C('id', CppTableId(STACK_PROFILE_CALLSITE_TABLE)),
        C('depth', CppUint32()),
        C('parent_id', CppOptional(CppTableId(STACK_PROFILE_CALLSITE_TABLE))),
        C('frame_id', CppTableId(STACK_PROFILE_FRAME_TABLE)),
        C("annotation", CppString()),
    ],
    add_implicit_column=False,
)

EXPERIMENTAL_SLICE_LAYOUT_TABLE = Table(
    python_module=__file__,
    class_name="ExperimentalSliceLayoutTable",
    sql_name="experimental_slice_layout",
    columns=[
        C('id', CppTableId(SLICE_TABLE)),
        C("layout_depth", CppUint32()),
    ],
    add_implicit_column=False,
)

DFS_WEIGHT_BOUNDED_TABLE = Table(
    python_module=__file__,
    class_name="DfsWeightBoundedTable",
    sql_name="__intrinsic_dfs_weight_bounded",
    columns=[
        C("root_node_id", CppUint32()),
        C("node_id", CppUint32()),
        C("parent_node_id", CppOptional(CppUint32())),
    ],
)

DATAFRAME_QUERY_PLAN_DECODER_TABLE_TABLE = Table(
    python_module=__file__,
    class_name="DataframeQueryPlanDecoderTable",
    sql_name="not_exposed_to_sql",
    columns=[
        C("bytecode_str", CppString()),
        C("serialized_bc", CppString(), flags=ColumnFlag.HIDDEN),
    ],
)

SURFACE_FLINGER_HIERARCHY_PATH_TABLE = Table(
    python_module=__file__,
    class_name="WinscopeSurfaceFlingerHierarchyPathTable",
    sql_name="__intrinsic_winscope_surfaceflinger_hierarchy_path",
    columns=[
        C('snapshot_id', CppUint32()),
        C('layer_id', CppUint32()),
        C('ancestor_id', CppUint32()),
    ],
)

# Keep this list sorted.
ALL_TABLES = [
    ANCESTOR_STACK_PROFILE_CALLSITE_TABLE,
    ARGS_WITH_DEFAULTS_TABLE,
    CONNECTED_FLOW_TABLE,
    DATAFRAME_QUERY_PLAN_DECODER_TABLE_TABLE,
    DFS_WEIGHT_BOUNDED_TABLE,
    EXPERIMENTAL_ANNOTATED_CALLSTACK_TABLE,
    EXPERIMENTAL_SLICE_LAYOUT_TABLE,
    SLICE_SUBSET_TABLE,
    SURFACE_FLINGER_HIERARCHY_PATH_TABLE,
    TABLE_INFO_TABLE,
]
