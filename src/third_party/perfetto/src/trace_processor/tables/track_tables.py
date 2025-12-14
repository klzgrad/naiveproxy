# Copyright (C) 2022 The Android Open Source Project
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
"""Contains tables for tracks."""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppSelfTableId
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import SqlAccess
from python.generators.trace_processor_table.public import Table

from src.trace_processor.tables.metadata_tables import MACHINE_TABLE
from src.trace_processor.tables.metadata_tables import THREAD_TABLE
from src.trace_processor.tables.metadata_tables import PROCESS_TABLE

TRACK_TABLE = Table(
    python_module=__file__,
    class_name="TrackTable",
    sql_name="__intrinsic_track",
    columns=[
        C(
            "name",
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            "parent_id",
            CppOptional(CppSelfTableId()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            "source_arg_set_id",
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C("machine_id", CppOptional(CppTableId(MACHINE_TABLE))),
        C(
            "type",
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            "dimension_arg_set_id",
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            "track_group_id",
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C("event_type", CppString()),
        C("counter_unit", CppOptional(CppString())),
        C(
            "utid",
            CppOptional(CppTableId(THREAD_TABLE)),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            "upid",
            CppOptional(CppTableId(PROCESS_TABLE)),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ])

# Keep this list sorted.
ALL_TABLES = [
    TRACK_TABLE,
]
