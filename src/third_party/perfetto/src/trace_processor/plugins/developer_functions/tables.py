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
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import Purpose
from python.generators.trace_processor_table.public import Table

DATAFRAME_QUERY_PLAN_DECODER_TABLE_TABLE = Table(
    python_module=__file__,
    class_name="DataframeQueryPlanDecoderTable",
    purpose=Purpose.STATIC_TABLE_FUNCTION,
    sql_name="not_exposed_to_sql",
    columns=[
        C("bytecode_str", CppString()),
        C("serialized_bc", CppString(), flags=ColumnFlag.HIDDEN),
    ],
)

# Keep this list sorted.
ALL_TABLES = [
    DATAFRAME_QUERY_PLAN_DECODER_TABLE_TABLE,
]
