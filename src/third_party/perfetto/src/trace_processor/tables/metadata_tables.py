# Copyright (C) 2022 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the 'License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an 'AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Contains metadata tables for a wide range of usecases."""

from python.generators.trace_processor_table.public import Alias
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import ColumnDoc
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppDouble
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppSelfTableId
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import SqlAccess
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc
from python.generators.trace_processor_table.public import WrappingSqlView

MACHINE_TABLE = Table(
    python_module=__file__,
    class_name='MachineTable',
    sql_name='machine',
    columns=[
        C('raw_id', CppUint32()),
        C(
            'sysname',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'release',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'version',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'arch',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'num_cpus',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'android_build_fingerprint',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'android_device_manufacturer',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'android_sdk_version',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          Contains raw machine_id of trace packets emitted from remote machines.
        ''',
        group='Metadata',
        columns={
            'raw_id':
                '''
                  Raw machine identifier in the trace packet, non-zero for
                  remote machines.
                ''',
            'sysname':
                '''
                  The name of the operating system (e.g. "Linux").
                ''',
            'release':
                '''
                  The current release of the operating system.
                ''',
            'version':
                '''
                  The current version of the operating system.
                ''',
            'arch':
                '''
                  Hardware architecture of the machine (e.g. x86_64).
                ''',
            'num_cpus':
                '''
                  Number of cpus available to the machine.
                ''',
            'android_build_fingerprint':
                '''
                  A unique string that identifies a specific build of
                  the Android operating system.
                ''',
            'android_device_manufacturer':
                '''
                  The Android's device manufacturer.
                ''',
            'android_sdk_version':
                '''
                  The Android SDK version used in the machine.
                ''',
        }))

PROCESS_TABLE = Table(
    python_module=__file__,
    class_name='ProcessTable',
    sql_name='__intrinsic_process',
    columns=[
        C('upid', Alias(underlying_column='id')),
        C('pid',
          CppInt64(),
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C(
            'name',
            CppOptional(CppString()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'start_ts',
            CppOptional(CppInt64()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'end_ts',
            CppOptional(CppInt64()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'parent_upid',
            CppOptional(CppSelfTableId()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'uid',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'android_appid',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'android_user_id',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'cmdline',
            CppOptional(CppString()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('machine_id', CppOptional(CppTableId(MACHINE_TABLE))),
    ],
    wrapping_sql_view=WrappingSqlView(view_name='process',),
    tabledoc=TableDoc(
        doc='Contains information of processes seen during the trace',
        group='Metadata',
        skip_id_and_type=True,
        columns={
            'upid':
                '''
                   Unique process id. This is != the OS pid. This is a
                   monotonic number associated to each process. The OS process
                   id (pid) cannot be used as primary key because tids and pids
                   are recycled by most kernels.
                ''',
            'pid':
                '''
                  The OS id for this process. Note: this is *not* unique
                  over the lifetime of the trace so cannot be used as a
                  primary key. Use |upid| instead.
                ''',
            'name':
                '''
                  The name of the process. Can be populated from many sources
                  (e.g. ftrace, /proc scraping, track event etc).
                ''',
            'start_ts':
                '''
                  The start timestamp of this process (if known). Is null
                  in most cases unless a process creation event is enabled
                  (e.g. task_newtask ftrace event on Linux/Android).
                ''',
            'end_ts':
                '''
                  The end timestamp of this process (if known). Is null in
                  most cases unless a process destruction event is enabled
                  (e.g. sched_process_free ftrace event on Linux/Android).
                ''',
            'parent_upid':
                ColumnDoc(
                    '''
                  The upid of the process which caused this process to be
                  spawned.
                ''',
                    joinable='process.upid'),
            'uid':
                ColumnDoc(
                    'The Unix user id of the process.',
                    joinable='package_list.uid'),
            'android_appid':
                'Android appid of this process.',
            'android_user_id':
                '''
                Android user id running the process.
                Related to Android multi-user (not to be confused with the
                unix uid)
                ''',
            'cmdline':
                '/proc/cmdline for this process.',
            'arg_set_id':
                ColumnDoc(
                    'Extra args for this process.', joinable='args.arg_set_id'),
            'machine_id':
                '''
                  Machine identifier, non-null for processes on a remote
                  machine.
                ''',
        },
    ),
)

THREAD_TABLE = Table(
    python_module=__file__,
    class_name='ThreadTable',
    sql_name='__intrinsic_thread',
    columns=[
        C('utid', Alias(underlying_column='id')),
        C('tid',
          CppInt64(),
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C(
            'name',
            CppOptional(CppString()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'start_ts',
            CppOptional(CppInt64()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'end_ts',
            CppOptional(CppInt64()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'upid',
            CppOptional(CppTableId(PROCESS_TABLE)),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'is_main_thread',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C('is_idle', CppUint32()),
        C(
            'machine_id',
            CppOptional(CppTableId(MACHINE_TABLE)),
            cpp_access=CppAccess.READ,
        ),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    wrapping_sql_view=WrappingSqlView(view_name='thread',),
    tabledoc=TableDoc(
        doc='Contains information of threads seen during the trace',
        group='Metadata',
        skip_id_and_type=True,
        columns={
            'utid':
                '''
                  Unique thread id. This is != the OS tid. This is a monotonic
                  number associated to each thread. The OS thread id (tid)
                  cannot be used as primary key because tids and pids are
                  recycled by most kernels.
                ''',
            'tid':
                '''
                  The OS id for this thread. Note: this is *not* unique over the
                  lifetime of the trace so cannot be used as a primary key. Use
                  |utid| instead.
                ''',
            'name':
                '''
                  The name of the thread. Can be populated from many sources
                  (e.g. ftrace, /proc scraping, track event etc).
                ''',
            'start_ts':
                '''
                  The start timestamp of this thread (if known). Is null in most
                  cases unless a thread creation event is enabled (e.g.
                  task_newtask ftrace event on Linux/Android).
                ''',
            'end_ts':
                '''
                  The end timestamp of this thread (if known). Is null in most
                  cases unless a thread destruction event is enabled (e.g.
                  sched_process_free ftrace event on Linux/Android).
                ''',
            'upid':
                ColumnDoc(
                    'The process hosting this thread.',
                    joinable='process.upid'),
            'is_main_thread':
                '''
                  Boolean indicating if this thread is the main thread
                  in the process.
                ''',
            'is_idle':
                '''
                  Boolean indicating if this thread is an kernel idle task (
                  pid = 0 on Linux).

                ''',
            'machine_id':
                '''
                  Machine identifier, non-null for threads on a remote machine.
                ''',
            'arg_set_id':
                ColumnDoc(
                    'Extra args for this thread.', joinable='args.arg_set_id'),
        },
    ),
)

CPU_TABLE = Table(
    python_module=__file__,
    class_name='CpuTable',
    sql_name='__intrinsic_cpu',
    columns=[
        C(
            'cpu',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'cluster_id',
            CppUint32(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'processor',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C('machine_id', CppOptional(CppTableId(MACHINE_TABLE))),
        C(
            'capacity',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
    ],
    wrapping_sql_view=WrappingSqlView('cpu'),
    tabledoc=TableDoc(
        doc='''
          Contains information of processes seen during the trace
        ''',
        group='Misc',
        columns={
            'cpu':
                '''the index (0-based) of the CPU core on the device''',
            'cluster_id':
                '''the cluster id is shared by CPUs in the same cluster''',
            'processor':
                '''a string describing this core''',
            'machine_id':
                '''
                  Machine identifier, non-null for CPUs on a remote machine.
                ''',
            'capacity':
                '''
                  Capacity of a CPU of a device, a metric which indicates the
                  relative performance of a CPU on a device
                  For details see:
                  https://www.kernel.org/doc/Documentation/devicetree/bindings/arm/cpu-capacity.txt
                ''',
            'arg_set_id':
                '''Extra args associated with the CPU''',
        }))

CHROME_RAW_TABLE = Table(
    python_module=__file__,
    class_name='ChromeRawTable',
    sql_name='__intrinsic_chrome_raw',
    columns=[
        C(
            'ts',
            CppInt64(),
            flags=ColumnFlag.SORTED,
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'name',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'utid',
            CppTableId(THREAD_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'arg_set_id',
            CppUint32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ])

FTRACE_EVENT_TABLE = Table(
    python_module=__file__,
    class_name='FtraceEventTable',
    sql_name='__intrinsic_ftrace_event',
    columns=[
        C('ts',
          CppInt64(),
          flags=ColumnFlag.SORTED,
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C(
            'name',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('utid',
          CppTableId(THREAD_TABLE),
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C(
            'arg_set_id',
            CppUint32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('common_flags', CppUint32()),
        C('ucpu',
          CppTableId(CPU_TABLE),
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
    ],
    wrapping_sql_view=WrappingSqlView('ftrace_event'),
    tabledoc=TableDoc(
        doc='''
          Contains all the ftrace events in the trace. This table exists only
          for debugging purposes and should not be relied on in production
          usecases (i.e. metrics, standard library etc). Note also that this
          table might be empty if raw ftrace parsing has been disabled.
        ''',
        group='Events',
        columns={
            'arg_set_id':
                ColumnDoc(
                    'The set of key/value pairs associated with this event.',
                    joinable='args.arg_set_id'),
            'ts':
                'The timestamp of this event.',
            'name':
                '''
                  The name of the event. For ftrace events, this will be the
                  ftrace event name.
                ''',
            'utid':
                'The thread this event was emitted on.',
            'common_flags':
                '''
                  Ftrace event flags for this event. Currently only emitted for
                  sched_waking events.
                ''',
            'ucpu':
                '''
                  The unique CPU indentifier.
                ''',
        }))

ARG_TABLE = Table(
    python_module=__file__,
    class_name='ArgTable',
    sql_name='__intrinsic_args',
    columns=[
        C(
            'arg_set_id',
            CppUint32(),
            flags=ColumnFlag.SORTED | ColumnFlag.SET_ID,
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
            sql_access=SqlAccess.HIGH_PERF,
        ),
        C(
            'flat_key',
            CppString(),
            cpp_access=CppAccess.READ,
        ),
        C(
            'key',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
            sql_access=SqlAccess.HIGH_PERF,
        ),
        C(
            'int_value',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'string_value',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'real_value',
            CppOptional(CppDouble()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'value_type',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
            sql_access=SqlAccess.HIGH_PERF,
        ),
    ],
    wrapping_sql_view=WrappingSqlView(view_name='args'),
    tabledoc=TableDoc(
        doc='''''',
        group='Misc',
        columns={
            'arg_set_id': '''''',
            'flat_key': '''''',
            'key': '''''',
            'int_value': '''''',
            'string_value': '''''',
            'real_value': '''''',
            'value_type': ''''''
        }))

METADATA_TABLE = Table(
    python_module=__file__,
    class_name='MetadataTable',
    sql_name='metadata',
    columns=[
        C(
            'name',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('key_type', CppString()),
        C(
            'int_value',
            CppOptional(CppInt64()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'str_value',
            CppOptional(CppString()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''''',
        group='Metadata',
        columns={
            'name': '''''',
            'key_type': '''''',
            'int_value': '''''',
            'str_value': ''''''
        }))

FILEDESCRIPTOR_TABLE = Table(
    python_module=__file__,
    class_name='FiledescriptorTable',
    sql_name='filedescriptor',
    columns=[
        C('ufd', CppInt64()),
        C('fd', CppInt64()),
        C('ts', CppOptional(CppInt64())),
        C('upid', CppOptional(CppUint32())),
        C('path', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='''
          Contains information of filedescriptors collected during the trace
        ''',
        group='Metadata',
        columns={
            'ufd':
                '''Unique fd. This is != the OS fd.
This is a monotonic number associated to each
filedescriptor. The OS assigned fd cannot be used as
primary key because fds are recycled by most kernels.''',
            'fd':
                '''The OS id for this process. Note: this is *not*
unique over the lifetime of the trace so cannot be
used as a primary key. Use |ufd| instead.''',
            'ts':
                '''The timestamp for when the fd was collected.''',
            'upid':
                ''' The upid of the process which
opened the filedescriptor.''',
            'path':
                '''The path to the file or device backing the fd
In case this was a socket the path will be the port
number.'''
        }))

EXP_MISSING_CHROME_PROC_TABLE = Table(
    python_module=__file__,
    class_name='ExpMissingChromeProcTable',
    sql_name='experimental_missing_chrome_processes',
    columns=[
        C('upid', CppUint32()),
        C('reliable_from', CppOptional(CppInt64())),
    ],
    tabledoc=TableDoc(
        doc='''
          Experimental table, subject to arbitrary breaking changes.
        ''',
        group='Chrome',
        columns={
            'upid': '''''',
            'reliable_from': ''''''
        }))

CPU_FREQ_TABLE = Table(
    python_module=__file__,
    class_name='CpuFreqTable',
    sql_name='__intrinsic_cpu_freq',
    columns=[
        C('ucpu', CppTableId(CPU_TABLE)),
        C('freq', CppUint32()),
    ],
    wrapping_sql_view=WrappingSqlView('cpu_freq'),
    tabledoc=TableDoc(
        doc='''''', group='Misc', columns={
            'ucpu': '''''',
            'freq': '''''',
        }))

CLOCK_SNAPSHOT_TABLE = Table(
    python_module=__file__,
    class_name='ClockSnapshotTable',
    sql_name='clock_snapshot',
    columns=[
        C(
            'ts',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'clock_id',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('clock_name', CppOptional(CppString())),
        C('clock_value',
          CppInt64(),
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C('snapshot_id', CppUint32()),
        C('machine_id', CppOptional(CppTableId(MACHINE_TABLE))),
    ],
    tabledoc=TableDoc(
        doc='''
          Contains all the mapping between clock snapshots and trace time.

NOTE: this table is not sorted by timestamp; this is why we omit the
sorted flag on the ts column.
        ''',
        group='Misc',
        columns={
            'ts':
                '''timestamp of the snapshot in trace time.''',
            'clock_id':
                '''id of the clock (corresponds to the id in the trace).''',
            'clock_name':
                '''the name of the clock for builtin clocks or null
otherwise.''',
            'clock_value':
                '''timestamp of the snapshot in clock time.''',
            'snapshot_id':
                '''the index of this snapshot (only useful for debugging)''',
            'machine_id':
                '''
                  Machine identifier, non-null for clock snapshots on a remote
                  machine.
                ''',
        }))

TRACE_FILE_TABLE = Table(
    python_module=__file__,
    class_name='TraceFileTable',
    sql_name='__intrinsic_trace_file',
    columns=[
        C('parent_id', CppOptional(CppSelfTableId())),
        C('name', CppOptional(CppString())),
        C('size', CppInt64(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C(
            'trace_type',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'processing_order',
            CppOptional(CppInt64()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
        ),
    ],
    wrapping_sql_view=WrappingSqlView('trace_file'),
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

BUILD_FLAGS_TABLE = Table(
    python_module=__file__,
    class_name='BuildFlagsTable',
    sql_name='__intrinsic_build_flags',
    columns=[
        C('name', CppString()),
        C('enabled', CppUint32()),
    ],
    tabledoc=TableDoc(
        doc='''
            Contains all the build flags used in the project.
        ''',
        group='Misc',
        columns={
            'name':
                '''Name of the build flag.''',
            'enabled':
                '''Whether the build flag is enabled (1) or disabled (0).''',
        }))

MODULES_TABLE = Table(
    python_module=__file__,
    class_name='ModulesTable',
    sql_name='__intrinsic_modules',
    columns=[
        C('name', CppString()),
    ],
    tabledoc=TableDoc(
        doc='''
            Contains all the modules enabled.
        ''',
        group='Misc',
        columns={
            'name': '''Name of the enabled module.''',
        }))

TRACE_IMPORT_LOGS_TABLE = Table(
    python_module=__file__,
    class_name='TraceImportLogsTable',
    sql_name='__intrinsic_trace_import_logs',
    columns=[
        C('trace_id', CppUint32()),
        C('ts', CppOptional(CppInt64())),
        C('byte_offset', CppOptional(CppInt64())),
        C('severity', CppString()),
        C('name', CppString()),
        C('arg_set_id', CppOptional(CppUint32())),
    ],
)

# Keep this list sorted.
ALL_TABLES = [
    ARG_TABLE,
    BUILD_FLAGS_TABLE,
    CHROME_RAW_TABLE,
    CLOCK_SNAPSHOT_TABLE,
    CPU_FREQ_TABLE,
    CPU_TABLE,
    EXP_MISSING_CHROME_PROC_TABLE,
    FILEDESCRIPTOR_TABLE,
    FTRACE_EVENT_TABLE,
    TRACE_IMPORT_LOGS_TABLE,
    MACHINE_TABLE,
    METADATA_TABLE,
    MODULES_TABLE,
    PROCESS_TABLE,
    THREAD_TABLE,
    TRACE_FILE_TABLE,
]
