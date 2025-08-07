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
        C("parent_node_id", CppOptional(CppUint32())),
    ])

DOMINATOR_TREE_TABLE = Table(
    python_module=__file__,
    class_name="DominatorTreeTable",
    sql_name="__intrinsic_dominator_tree",
    columns=[
        C("node_id", CppUint32()),
        C("dominator_node_id", CppOptional(CppUint32())),
    ])

STRUCTURAL_TREE_PARTITION_TABLE = Table(
    python_module=__file__,
    class_name="StructuralTreePartitionTable",
    sql_name="__intrinsic_structural_tree_partition",
    columns=[
        C("node_id", CppUint32()),
        C("parent_node_id", CppOptional(CppUint32())),
        C("group_key", CppUint32()),
    ])

# Keep this list sorted.
ALL_TABLES = [
    DOMINATOR_TREE_TABLE,
    STRUCTURAL_TREE_PARTITION_TABLE,
    TREE_TABLE,
]
