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
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Purpose
from python.generators.trace_processor_table.public import Table

DFS_WEIGHT_BOUNDED_TABLE = Table(
    python_module=__file__,
    class_name="DfsWeightBoundedTable",
    purpose=Purpose.STATIC_TABLE_FUNCTION,
    sql_name="__intrinsic_dfs_weight_bounded",
    columns=[
        C("root_node_id", CppUint32()),
        C("node_id", CppUint32()),
        C("parent_node_id", CppOptional(CppUint32())),
    ],
)

# Keep this list sorted.
ALL_TABLES = [
    DFS_WEIGHT_BOUNDED_TABLE,
]
