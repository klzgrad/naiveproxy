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
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Purpose
from python.generators.trace_processor_table.public import Table

from src.trace_processor.tables.profiler_tables import STACK_PROFILE_CALLSITE_TABLE
from src.trace_processor.tables.profiler_tables import STACK_PROFILE_FRAME_TABLE
from src.trace_processor.tables.slice_tables import SLICE_TABLE
from src.trace_processor.tables.track_tables import TRACK_TABLE

SLICE_SUBSET_TABLE = Table(
    python_module=__file__,
    class_name="SliceSubsetTable",
    purpose=Purpose.STATIC_TABLE_FUNCTION,
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
    purpose=Purpose.STATIC_TABLE_FUNCTION,
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

# Keep this list sorted.
ALL_TABLES = [
    ANCESTOR_STACK_PROFILE_CALLSITE_TABLE,
    SLICE_SUBSET_TABLE,
]
