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
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppDouble
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Purpose
from python.generators.trace_processor_table.public import Table

ARGS_WITH_DEFAULTS_TABLE = Table(
    python_module=__file__,
    class_name='WinscopeArgsWithDefaultsTable',
    purpose=Purpose.STATIC_TABLE_FUNCTION,
    sql_name='__intrinsic_winscope_proto_to_args_with_defaults',
    columns=[
        C("table_name",
          CppString(),
          flags=ColumnFlag.HIDDEN,
          cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
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

# Keep this list sorted.
ALL_TABLES = [
    ARGS_WITH_DEFAULTS_TABLE,
]
