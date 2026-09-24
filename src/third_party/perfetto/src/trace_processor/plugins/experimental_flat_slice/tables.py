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
from python.generators.trace_processor_table.public import ColumnDoc
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Purpose
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc

from src.trace_processor.tables.slice_tables import SLICE_TABLE
from src.trace_processor.tables.track_tables import TRACK_TABLE

EXPERIMENTAL_FLAT_SLICE_TABLE = Table(
    python_module=__file__,
    purpose=Purpose.STATIC_TABLE_FUNCTION,
    class_name='ExperimentalFlatSliceTable',
    sql_name='experimental_flat_slice',
    columns=[
        C('ts', CppInt64(), cpp_access=CppAccess.READ),
        C('dur', CppInt64(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C('track_id', CppTableId(TRACK_TABLE)),
        C('category', CppOptional(CppString())),
        C('name', CppOptional(CppString())),
        C('arg_set_id', CppOptional(CppUint32())),
        C('source_id', CppOptional(CppTableId(SLICE_TABLE))),
        C('start_bound', CppInt64(), flags=ColumnFlag.HIDDEN),
        C('end_bound', CppInt64(), flags=ColumnFlag.HIDDEN),
    ],
    tabledoc=TableDoc(
        doc='''
          An experimental table which "flattens" stacks of slices to contain
          only the "deepest" slice at any point in time on each track.
        ''',
        group='Slice',
        columns={
            'ts':
                '''The timestamp at the start of the slice (in nanoseconds).''',
            'dur':
                '''The duration of the slice (in nanoseconds).''',
            'track_id':
                'The id of the track this slice is located on.',
            'category':
                '''
                  The "category" of the slice. If this slice originated with
                  track_event, this column contains the category emitted.
                  Otherwise, it is likely to be null (with limited exceptions).
                ''',
            'name':
                '''
                  The name of the slice. The name describes what was happening
                  during the slice.
                ''',
            'arg_set_id':
                ColumnDoc(
                    'The id of the argument set associated with this slice.',
                    joinable='args.arg_set_id'),
            'source_id':
                'The id of the slice which this row originated from.',
        }))

# Keep this list sorted.
ALL_TABLES = [
    EXPERIMENTAL_FLAT_SLICE_TABLE,
]
