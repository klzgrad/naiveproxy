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
"""Contains tables related to Jitted code.

These tables are WIP, the schema is not stable and you should not rely on them
for any serious business just yet""
"""

from python.generators.trace_processor_table.public import Alias
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc

from src.trace_processor.tables.profiler_tables import STACK_PROFILE_FRAME_TABLE

JIT_CODE_TABLE = Table(
    python_module=__file__,
    class_name='JitCodeTable',
    sql_name='__intrinsic_jit_code',
    columns=[
        C(
            'create_ts',
            CppInt64(),
            flags=ColumnFlag.SORTED,
            cpp_access=CppAccess.READ,
        ),
        C(
            'estimated_delete_ts',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C('utid', CppUint32(), cpp_access=CppAccess.READ),
        C('start_address', CppInt64(), cpp_access=CppAccess.READ),
        C('size', CppInt64(), cpp_access=CppAccess.READ),
        C(
            'function_name',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('native_code_base64', CppOptional(CppString())),
        C('jit_code_id', Alias('id')),
    ],
    tabledoc=TableDoc(
        doc="""
          Represents a jitted code snippet
        """,
        group='jit',
        columns={
            'create_ts': """Time this code was created / allocated""",
            'estimated_delete_ts':
                ("""Time this code was destroyed / deallocated. This is an upper
                bound, as we can only detect deletions indirectly when new code
                is allocated overlapping existing one.
                """),
            'utid': 'Thread that generated the code',
            'start_address': 'Start address for the generated code',
            'size': 'Size in bytes of the generated code',
            'function_name': 'Function name',
            'native_code_base64': 'Jitted code base64 encoded',
            'jit_code_id': 'Alias for id. Makes joins easier',
        },
    ),
)

JIT_FRAME_TABLE = Table(
    python_module=__file__,
    class_name='JitFrameTable',
    sql_name='__intrinsic_jit_frame',
    columns=[
        C(
            'jit_code_id',
            CppTableId(JIT_CODE_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'frame_id',
            CppTableId(STACK_PROFILE_FRAME_TABLE),
            cpp_access=CppAccess.READ,
        ),
    ],
    tabledoc=TableDoc(
        doc="""
          Represents a jitted frame
        """,
        group='jit',
        columns={
            'jit_code_id': 'Jitted code snipped the frame is in',
            'frame_id': 'Jitted frame',
        },
    ),
)

# Keep this list sorted.
ALL_TABLES = [JIT_CODE_TABLE, JIT_FRAME_TABLE]
