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
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import Purpose
from python.generators.trace_processor_table.public import Table

STDLIB_DOCS_OBJECTS_TABLE = Table(
    python_module=__file__,
    class_name="StdlibDocsObjectsTable",
    purpose=Purpose.STATIC_TABLE_FUNCTION,
    sql_name="not_exposed_to_sql",
    columns=[
        C("package", CppString()),
        C("module", CppString()),
        C("name", CppString()),
        C("qualified_name", CppString()),
        C("object_type", CppString()),
        C("exposed", CppInt64()),
        C("short_description", CppString()),
        C("summary", CppString()),
        C("description", CppString()),
        C("return_type", CppString()),
        C("return_description", CppString()),
        C("args", CppString()),
        C("cols", CppString()),
    ],
)

ALL_TABLES = [STDLIB_DOCS_OBJECTS_TABLE]
