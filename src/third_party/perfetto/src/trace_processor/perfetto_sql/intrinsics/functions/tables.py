# Copyright (C) 2024 The Android Open Source Project
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
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table

# This class should contain all table schemas for functions (scalar or
# aggregate) which return tables.

# Helper table to return any sort of "tree-like" table from functions.
TREE_TABLE = Table(
    python_module=__file__,
    class_name="TreeTable",
    sql_name="__unused",
    columns=[
        C("node_id", CppUint32()),
        C("parent_node_id",
          CppOptional(CppUint32()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
    ])

DOMINATOR_TREE_TABLE = Table(
    python_module=__file__,
    class_name="DominatorTreeTable",
    sql_name="__intrinsic_dominator_tree",
    columns=[
        C("node_id", CppUint32()),
        C("dominator_node_id",
          CppOptional(CppUint32()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
    ])

STRUCTURAL_TREE_PARTITION_TABLE = Table(
    python_module=__file__,
    class_name="StructuralTreePartitionTable",
    sql_name="__intrinsic_structural_tree_partition",
    columns=[
        C("node_id", CppUint32()),
        C("parent_node_id",
          CppOptional(CppUint32()),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C("group_key", CppUint32()),
    ])

# Output of `__intrinsic_critical_path_walk`. One row per emitted
# blocker frame: thread `blocker_utid` (wakeup-graph entry `blocker_id`)
# was on-CPU on `root_id`'s behalf during [ts, ts + dur), at `depth`
# cross-thread hops along the wakeup chain. `parent_id` is the
# `blocker_id` of the layer one level above this row (`root_id` at
# depth 0); callers feed the rows to `_intervals_flatten` to collapse
# overlapping layers to the deepest active blocker per timestamp.
CRITICAL_PATH_WALK_TABLE = Table(
    python_module=__file__,
    class_name="CriticalPathWalkTable",
    sql_name="__intrinsic_critical_path_walk_out",
    columns=[
        C("root_id", CppUint32(),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C("depth", CppUint32(), cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C("ts", CppInt64(), cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C("dur", CppInt64(), cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C("blocker_id",
          CppUint32(),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C("blocker_utid",
          CppUint32(),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C("parent_id",
          CppUint32(),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
    ])

# Keep this list sorted.
ALL_TABLES = [
    CRITICAL_PATH_WALK_TABLE,
    DOMINATOR_TREE_TABLE,
    STRUCTURAL_TREE_PARTITION_TABLE,
    TREE_TABLE,
]
