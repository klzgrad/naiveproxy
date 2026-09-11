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
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Purpose
from python.generators.trace_processor_table.public import Table

from src.trace_processor.tables.profiler_tables import STACK_PROFILE_CALLSITE_TABLE
from src.trace_processor.tables.profiler_tables import STACK_PROFILE_FRAME_TABLE

EXPERIMENTAL_ANNOTATED_CALLSTACK_TABLE = Table(
    python_module=__file__,
    class_name="ExperimentalAnnotatedCallstackTable",
    purpose=Purpose.STATIC_TABLE_FUNCTION,
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

# Keep this list sorted.
ALL_TABLES = [
    EXPERIMENTAL_ANNOTATED_CALLSTACK_TABLE,
]
