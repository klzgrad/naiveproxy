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
"""Contains tables for log entries from various sources."""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import ColumnDoc
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc

from src.trace_processor.tables.metadata_tables import THREAD_TABLE

LOG_TABLE = Table(
    python_module=__file__,
    class_name="LogTable",
    sql_name="__intrinsic_logs",
    columns=[
        C("ts", CppInt64(), cpp_access=CppAccess.READ),
        C("utid",
          CppOptional(CppTableId(THREAD_TABLE)),
          cpp_access=CppAccess.READ),
        C("prio", CppUint32(), cpp_access=CppAccess.READ),
        C("log_source", CppString(), cpp_access=CppAccess.READ),
        C("tag", CppOptional(CppString()), cpp_access=CppAccess.READ),
        C("msg", CppString(), cpp_access=CppAccess.READ),
        C("arg_set_id",
          CppOptional(CppUint32()),
          cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
    ],
    tabledoc=TableDoc(
        doc='''
          Log entries from all sources (Android logcat, journald, etc.).

          NOTE: this table is not sorted by timestamp. This is why we omit the
          sorted flag on the ts column.
        ''',
        group='Android',
        columns={
            'ts':
                'Timestamp of the log entry.',
            'utid':
                ColumnDoc(
                    doc='Thread writing the log entry (nullable).',
                    joinable='thread.id'),
            'prio':
                'Priority. Android: 3=DEBUG..6=ERROR. Journald/syslog: 0=EMERG..7=DEBUG.',
            'log_source':
                "Source of the log entry: 'android_logcat' or 'systemd_journald'.",
            'tag':
                'Tag / SYSLOG_IDENTIFIER of the log entry.',
            'msg':
                'Content of the log entry.',
            'arg_set_id':
                ColumnDoc(
                    doc='Args for source-specific metadata.',
                    joinable='args.arg_set_id'),
        },
    ),
)

ALL_TABLES = [LOG_TABLE]
