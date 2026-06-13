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
"""
Contains tables related to perf data ingestion.
"""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import ColumnDoc
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc

ETM_V4_CONFIGURATION = Table(
    python_module=__file__,
    class_name='EtmV4ConfigurationTable',
    sql_name='__intrinsic_etm_v4_configuration',
    columns=[
        C('set_id', CppUint32(), flags=ColumnFlag.SORTED | ColumnFlag.SET_ID),
        C('cpu', CppUint32(), cpp_access=CppAccess.READ),
        C('cs_trace_stream_id', CppUint32(), cpp_access=CppAccess.READ),
        C('core_profile', CppString()),
        C('arch_version', CppString()),
        C('major_version', CppUint32()),
        C('minor_version', CppUint32()),
        C('max_speculation_depth', CppUint32()),
        C('bool_flags', CppInt64()),
    ],
    tabledoc=TableDoc(
        doc='''
          This table tracks ETM configurations. Rows are grouped by a set_id
          to represent the configurations of each of the CPUs.
        ''',
        group='ETM',
        columns={
            'set_id':
                '''
                  Groups all configuration ros that belong to the same ETM trace.
                  There is one row per each CPU where ETM was configured.
                ''',
            'cpu':
                'CPU this configuration applies to.',
            'cs_trace_stream_id':
                'Trace Stream ID register',
            'core_profile':
                'Core Profile (e.g. Cortex-A or Cortex-M)',
            'arch_version':
                'Architecture version (e.g. AA64)',
            'major_version':
                'Major version',
            'minor_version':
                'Minor version',
            'max_speculation_depth':
                'Maximum speculation depth of the core',
            'bool_flags':
                'Collection of boolean flags.',
        },
    ))

ETM_V4_SESSION = Table(
    python_module=__file__,
    class_name='EtmV4SessionTable',
    sql_name='__intrinsic_etm_v4_session',
    columns=[
        C(
            'configuration_id',
            CppTableId(ETM_V4_CONFIGURATION),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'start_ts',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          Represents a trace session on one core. From time the tracing is
          started to when it is stopped.
        ''',
        group='ETM',
        columns={
            'configuration_id':
                ColumnDoc(
                    'ETM configuration',
                    joinable='__intrinsic_etm_v4_configuration.id'),
            'start_ts':
                'COCK_MONOTONIC timestamp for when the ETM session collection started.',
        },
    ))

ETM_V4_CHUNK = Table(
    python_module=__file__,
    class_name='EtmV4ChunkTable',
    sql_name='__intrinsic_etm_v4_chunk',
    columns=[
        C('session_id',
          CppTableId(ETM_V4_SESSION),
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C('chunk_set_id',
          CppUint32(),
          flags=ColumnFlag.SORTED | ColumnFlag.SET_ID),
        C('size', CppInt64()),
    ],
    tabledoc=TableDoc(
        doc='''
          Represents a contiguous chunk of ETM chunk data for a core. The data
          collected during a session might be split into different chunks in the
          case of data loss.
        ''',
        group='ETM',
        columns={
            'session_id':
                ColumnDoc(
                    'Session this data belongs to',
                    joinable='__intrinsic_etm_v4_chunk.id'),
            'chunk_set_id':
                'Groups all the chunks belonging to the same session.',
            'size':
                'Size in bytes',
        },
    ))

FILE_TABLE = Table(
    python_module=__file__,
    class_name='FileTable',
    sql_name='__intrinsic_file',
    columns=[
        C('name', CppString()),
        C(
            'size',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'trace_type',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
            Metadata related to the trace file parsed. Note the order in which
            the files appear in this table corresponds to the order in which
            they are read and sent to the tokenization stage.
        ''',
        group='Misc',
        columns={
            'parent_id':
                '''
                  Parent file. E.g. files contained in a zip file will point to
                  the zip file.
                ''',
            'name':
                '''File name, if known, NULL otherwise''',
            'size':
                '''Size in bytes''',
            'trace_type':
                '''Trace type''',
            'processing_order':
                '''In which order where the files were processed.''',
        }))

ELF_FILE_TABLE = Table(
    python_module=__file__,
    class_name='ElfFileTable',
    sql_name='__intrinsic_elf_file',
    columns=[
        C('file_id', CppTableId(FILE_TABLE), cpp_access=CppAccess.READ),
        C('load_bias', CppInt64()),
        C('build_id', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='''
            Metadata related to the trace file parsed. Note the order in which
            the files appear in this table corresponds to the order in which
            they are read and sent to the tokenization stage.
        ''',
        group='Misc',
        columns={
            'parent_id':
                '''
                  Parent file. E.g. files contained in a zip file will point to
                  the zip file.
                ''',
            'name':
                '''File name, if known, NULL otherwise''',
            'size':
                '''Size in bytes''',
            'trace_type':
                '''Trace type''',
            'processing_order':
                '''In which order where the files were processed.''',
        }))

ALL_TABLES = [
    ETM_V4_CONFIGURATION, ETM_V4_CHUNK, ETM_V4_SESSION, FILE_TABLE,
    ELF_FILE_TABLE
]
