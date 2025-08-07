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
"""Contains tables for relevant for sched."""

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppInt32
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppSelfTableId
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc
from python.generators.trace_processor_table.public import WrappingSqlView

from src.trace_processor.tables.metadata_tables import CPU_TABLE

SCHED_SLICE_TABLE = Table(
    python_module=__file__,
    class_name='SchedSliceTable',
    sql_name='__intrinsic_sched_slice',
    columns=[
        C('ts',
          CppInt64(),
          flags=ColumnFlag.SORTED,
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C('dur',
          CppInt64(),
          cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C('utid', CppUint32(), cpp_access=CppAccess.READ),
        C(
            'end_state',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C('priority', CppInt32()),
        C('ucpu', CppTableId(CPU_TABLE)),
    ],
    wrapping_sql_view=WrappingSqlView('sched'),
    tabledoc=TableDoc(
        doc='''
          This table holds slices with kernel thread scheduling information.
          These slices are collected when the Linux "ftrace" data source is
          used with the "sched/switch" and "sched/wakeup*" events enabled.

          The rows in this table will always have a matching row in the
          |thread_state| table with |thread_state.state| = 'Running'
        ''',
        group='Events',
        columns={
            'ts':
                '''The timestamp at the start of the slice (in nanoseconds).''',
            'dur':
                '''The duration of the slice (in nanoseconds).''',
            'utid':
                '''The thread's unique id in the trace.''',
            'end_state':
                '''
                  A string representing the scheduling state of the kernel
                  thread at the end of the slice.  The individual characters in
                  the string mean the following: R (runnable), S (awaiting a
                  wakeup), D (in an uninterruptible sleep), T (suspended),
                  t (being traced), X (exiting), P (parked), W (waking),
                  I (idle), N (not contributing to the load average),
                  K (wakeable on fatal signals) and Z (zombie, awaiting
                  cleanup).
                ''',
            'priority':
                '''The kernel priority that the thread ran at.''',
            'ucpu':
                '''
                  The unique CPU identifier that the slice executed on.
                ''',
        }))

SPURIOUS_SCHED_WAKEUP_TABLE = Table(
    python_module=__file__,
    class_name='SpuriousSchedWakeupTable',
    sql_name='spurious_sched_wakeup',
    columns=[
        C('ts', CppInt64(), flags=ColumnFlag.SORTED),
        C('thread_state_id', CppInt64()),
        C('irq_context', CppOptional(CppUint32())),
        C('utid', CppUint32()),
        C('waker_utid', CppUint32())
    ],
    tabledoc=TableDoc(
        doc='''
          This table contains the scheduling wakeups that occurred while a
          thread was not blocked, i.e. running or runnable. Such wakeups are not
          tracked in the |thread_state_table|.
        ''',
        group='Events',
        columns={
            'ts':
                'The timestamp at the start of the slice (in nanoseconds).',
            'thread_state_id':
                '''
                  The id of the row in the thread_state table that this row is
                  associated with.
                ''',
            'irq_context':
                '''
                  Whether the wakeup was from interrupt context or process
                  context.
                ''',
            'utid':
                '''The thread's unique id in the trace..''',
            'waker_utid':
                '''
                  The unique thread id of the thread which caused a wakeup of
                  this thread.
                ''',
        }))

THREAD_STATE_TABLE = Table(
    python_module=__file__,
    class_name='ThreadStateTable',
    sql_name='__intrinsic_thread_state',
    columns=[
        C('ts',
          CppInt64(),
          flags=ColumnFlag.SORTED,
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C('dur',
          CppInt64(),
          cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C('utid', CppUint32(), cpp_access=CppAccess.READ),
        C('state', CppString(), cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE),
        C(
            'io_wait',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'blocked_function',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'waker_utid',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C('waker_id', CppOptional(CppSelfTableId())),
        C(
            'irq_context',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'ucpu',
            CppOptional(CppTableId(CPU_TABLE)),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    wrapping_sql_view=WrappingSqlView('thread_state'),
    tabledoc=TableDoc(
        doc='''
          This table contains the scheduling state of every thread on the
          system during the trace.

          The rows in this table which have |state| = 'Running', will have a
          corresponding row in the |sched_slice| table.
        ''',
        group='Events',
        columns={
            'ts':
                'The timestamp at the start of the slice (in nanoseconds).',
            'dur':
                'The duration of the slice (in nanoseconds).',
            'utid':
                '''The thread's unique id in the trace.''',
            'state':
                '''
                  The scheduling state of the thread. Can be "Running" or any
                  of the states described in |sched_slice.end_state|.
                ''',
            'io_wait':
                'Indicates whether this thread was blocked on IO.',
            'blocked_function':
                'The function in the kernel this thread was blocked on.',
            'waker_utid':
                '''
                  The unique thread id of the thread which caused a wakeup of
                  this thread.
                ''',
            'waker_id':
                '''
                  The unique thread state id which caused a wakeup of this
                  thread.
                ''',
            'irq_context':
                '''
                  Whether the wakeup was from interrupt context or process
                  context.
                ''',
            'ucpu':
                '''
                  The unique CPU identifier that the thread executed on.
                ''',
        }))

# Keep this list sorted.
ALL_TABLES = [
    SCHED_SLICE_TABLE,
    SPURIOUS_SCHED_WAKEUP_TABLE,
    THREAD_STATE_TABLE,
]
