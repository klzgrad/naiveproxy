# Copyright (C) 2023 The Android Open Source Project
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
"""Contains tables for relevant for TODO."""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppSelfTableId
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc

EXPERIMENTAL_PROTO_PATH_TABLE = Table(
    python_module=__file__,
    class_name='ExperimentalProtoPathTable',
    sql_name='experimental_proto_path',
    columns=[
        C('parent_id', CppOptional(CppSelfTableId())),
        C('field_type', CppString()),
        C('field_name', CppOptional(CppString())),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          Experimental table, subject to arbitrary breaking changes.
        ''',
        group='Proto',
        columns={
            'parent_id': '''''',
            'field_type': '''''',
            'field_name': '''''',
            'arg_set_id': ''''''
        }))

EXPERIMENTAL_PROTO_CONTENT_TABLE = Table(
    python_module=__file__,
    class_name='ExperimentalProtoContentTable',
    sql_name='experimental_proto_content',
    columns=[
        C('path', CppString()),
        C('path_id', CppTableId(EXPERIMENTAL_PROTO_PATH_TABLE)),
        C('total_size', CppInt64()),
        C('size', CppInt64()),
        C('count', CppInt64()),
    ],
    tabledoc=TableDoc(
        doc='''''',
        group='Proto',
        columns={
            'path': '''''',
            'path_id': '''''',
            'total_size': '''''',
            'size': '''''',
            'count': ''''''
        }))

# Keep this list sorted.
ALL_TABLES = [
    EXPERIMENTAL_PROTO_CONTENT_TABLE,
    EXPERIMENTAL_PROTO_PATH_TABLE,
]
