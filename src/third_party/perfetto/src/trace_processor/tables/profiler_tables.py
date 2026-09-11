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
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import CppInt32
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppSelfTableId
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import CppUint32 as CppBool
from python.generators.trace_processor_table.public import CppDouble
from python.generators.trace_processor_table.public import SqlAccess
from python.generators.trace_processor_table.public import Purpose
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc
from python.generators.trace_processor_table.public import WrappingSqlView

from src.trace_processor.tables.counter_tables import COUNTER_TABLE
from src.trace_processor.tables.track_tables import TRACK_TABLE

PROFILER_SMAPS_TABLE = Table(
    python_module=__file__,
    class_name='ProfilerSmapsTable',
    sql_name='__intrinsic_profiler_smaps',
    wrapping_sql_view=WrappingSqlView('profiler_smaps'),
    columns=[
        C(
            'upid',
            CppUint32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'ts',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('path', CppString()),
        C('path_trimmed', CppString()),
        C('aggregate_count', CppUint32()),
        C('is_deleted', CppBool()),
        C(
            'size_kb',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'private_dirty_kb',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'swap_kb',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'file_name',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'start_address',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'module_timestamp',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'module_debugid',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'module_debug_path',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'protection_flags',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'private_clean_resident_kb',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'shared_dirty_resident_kb',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'shared_clean_resident_kb',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('locked_kb', CppInt64()),
        C(
            'proportional_resident_kb',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('rss_kb', CppInt64()),
        C('anonymous_kb', CppInt64()),
        C('pss_dirty_kb', CppInt64()),
        C('swap_pss_kb', CppInt64()),
    ],
    tabledoc=TableDoc(
        doc='''
          The profiler smaps contains the memory stats for virtual memory ranges
          captured by the
          [heap profiler](/docs/data-sources/native-heap-profiler.md).
        ''',
        group='Callstack profilers',
        columns={
            'upid':
                '''The unique PID of the process.''',
            'ts':
                '''Timestamp of the snapshot. Multiple rows will have the same
                timestamp.''',
            'path':
                '''The name of the mapping, as per /proc/pid/smaps.''',
            'path_trimmed':
                '''Same as `path` but with any trailing " (deleted)" suffix
                removed.''',
            'aggregate_count':
                '''''',
            'is_deleted':
                '''''',
            'size_kb':
                '''Total size of the mapping.''',
            'private_dirty_kb':
                '''KB of this mapping that are private dirty  RSS.''',
            'swap_kb':
                '''KB of this mapping that are in swap.''',
            'file_name':
                '''''',
            'start_address':
                '''''',
            'module_timestamp':
                '''''',
            'module_debugid':
                '''''',
            'module_debug_path':
                '''''',
            'protection_flags':
                '''''',
            'private_clean_resident_kb':
                '''''',
            'shared_dirty_resident_kb':
                '''''',
            'shared_clean_resident_kb':
                '''''',
            'locked_kb':
                '''''',
            'proportional_resident_kb':
                '''''',
            'rss_kb':
                '''''',
            'anonymous_kb':
                '''''',
            'pss_dirty_kb':
                '''''',
            'swap_pss_kb':
                '''''',
        }))

PACKAGE_LIST_TABLE = Table(
    python_module=__file__,
    class_name='PackageListTable',
    sql_name='__intrinsic_package_list',
    wrapping_sql_view=WrappingSqlView('package_list'),
    columns=[
        C(
            'package_name',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'uid',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'debuggable',
            CppInt32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'profileable_from_shell',
            CppInt32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'version_code',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          Metadata about packages installed on the system.
          This is generated by the packages_list data-source.\n
          Include this data-source in the perfetto config:

          ```
          data_sources {
          config {
          name: "android.packages_list"
          }
          }
          ```
        ''',
        group='Misc',
        columns={
            'package_name':
                '''name of the package, e.g. com.google.android.gm.''',
            'uid':
                '''UID processes of this package run as.''',
            'debuggable':
                '''bool whether this app is debuggable.''',
            'profileable_from_shell':
                '''bool whether this app is profileable.''',
            'version_code':
                '''versionCode from the APK.'''
        },
    ),
)

STACK_PROFILE_MAPPING_TABLE = Table(
    python_module=__file__,
    class_name='StackProfileMappingTable',
    sql_name='__intrinsic_stack_profile_mapping',
    wrapping_sql_view=WrappingSqlView('stack_profile_mapping'),
    columns=[
        C(
            'build_id',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'exact_offset',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'start_offset',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'start',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'end',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'load_bias',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'name',
            CppString(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          A mapping (binary / library) in a process.
          This is generated by the stack profilers: heapprofd and traced_perf.
        ''',
        group='Callstack profilers',
        columns={
            'build_id': '''Hex-encoded Build ID of the binary / library.''',
            'start': '''Start of the mapping in the process' address space.''',
            'end': '''End of the mapping in the process' address space.''',
            'name': '''Filename of the binary / library.''',
            'exact_offset': '''''',
            'start_offset': '''''',
            'load_bias': ''''''
        }))

STACK_PROFILE_FRAME_TABLE = Table(
    python_module=__file__,
    class_name='StackProfileFrameTable',
    sql_name='__intrinsic_stack_profile_frame',
    wrapping_sql_view=WrappingSqlView('stack_profile_frame'),
    columns=[
        C(
            'name',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'mapping',
            CppTableId(STACK_PROFILE_MAPPING_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'rel_pc',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'symbol_set_id',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
            flags=ColumnFlag.DENSE,
        ),
        C(
            'deobfuscated_name',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'type',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          A frame on the callstack. This is a location in a program.
          This is generated by the stack profilers: heapprofd and traced_perf.
        ''',
        group='Callstack profilers',
        columns={
            'name':
                '''Name of the function this location is in.''',
            'mapping':
                '''The mapping (library / binary) this location is in.''',
            'rel_pc':
                '''The program counter relative to the start of the mapping.''',
            'symbol_set_id':
                '''If the profile was offline symbolized, the offline
                symbol information of this frame.''',
            'deobfuscated_name':
                '''Deobfuscated name of the function this location is in.''',
            'type':
                '''The kind of frame (e.g. "native", "kernel", "interpreted",
                "jit", "gc", "runtime") if reported by the producer, else NULL.'''
        }))

STACK_PROFILE_CALLSITE_TABLE = Table(
    python_module=__file__,
    class_name='StackProfileCallsiteTable',
    sql_name='__intrinsic_stack_profile_callsite',
    wrapping_sql_view=WrappingSqlView('stack_profile_callsite'),
    columns=[
        C(
            'depth',
            CppUint32(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'parent_id',
            CppOptional(CppSelfTableId()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'frame_id',
            CppTableId(STACK_PROFILE_FRAME_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          A callsite. This is a list of frames that were on the stack.
          This is generated by the stack profilers: heapprofd and traced_perf.
        ''',
        group='Callstack profilers',
        columns={
            'depth':
                '''Distance from the bottom-most frame of the callstack.''',
            'parent_id':
                '''Parent frame on the callstack. NULL for the bottom-most.''',
            'frame_id':
                '''Frame at this position in the callstack.'''
        }))

PROFILER_ASYNC_CONTEXT_TABLE = Table(
    python_module=__file__,
    class_name='ProfilerAsyncContextTable',
    sql_name='__intrinsic_profiler_async_context',
    columns=[
        C('name', CppOptional(CppString())),
        C('kind', CppOptional(CppString())),
        C('parent_id', CppOptional(CppSelfTableId())),
    ],
    tabledoc=TableDoc(
        doc='''A stackful asynchronous execution context, such as a goroutine,
               fiber or coroutine.''',
        group='Callstack profilers',
        columns={
            'name':
                '''Human-readable name of this asynchronous context.''',
            'kind':
                '''Kind of asynchronous context, e.g. goroutine or fiber.''',
            'parent_id':
                '''Structural parent of this asynchronous context.''',
        }))

PROFILER_TASK_CONTEXT_TABLE = Table(
    python_module=__file__,
    class_name='ProfilerTaskContextTable',
    sql_name='__intrinsic_profiler_task_context',
    columns=[
        C(
            'upid',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'utid',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'async_context_id',
            CppOptional(CppTableId(PROFILER_ASYNC_CONTEXT_TABLE)),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''The task a profiler sample is attributed to: an OS process,
               thread and/or stackful asynchronous context.''',
        group='Callstack profilers',
        columns={
            'upid':
                '''Process the sample is attributed to, if known.''',
            'utid':
                '''Thread the sample is attributed to, if known.''',
            'async_context_id':
                '''Stackful asynchronous context the sample is
                                   attributed to, if any.''',
        }))

PROFILER_EXECUTION_CONTEXT_TABLE = Table(
    python_module=__file__,
    class_name='ProfilerExecutionContextTable',
    sql_name='__intrinsic_profiler_execution_context',
    columns=[
        C('ucpu', CppOptional(CppUint32())),
        C('cpu_mode', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='''The execution state in which a profiler sample was captured.''',
        group='Callstack profilers',
        columns={
            'ucpu': '''CPU the sample was captured on, if known.''',
            'cpu_mode': '''Privilege mode at the sample point, if known.''',
        }))

PROFILER_SESSION_TABLE = Table(
    python_module=__file__,
    class_name='ProfilerSessionTable',
    sql_name='__intrinsic_profiler_session',
    columns=[
        C('source', CppString()),
        C(
            'timebase_unit',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'cmdline',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''Profiler sessions: one row per data source instance of a
               sampling profiler (a perf session, a StackSample stream, ...).
            ''',
        group='Callstack profilers',
        columns={
            'source':
                '''The profiler that produced this session's samples (e.g.
                   "linux.perf"). Matches profiler_sample.source.''',
            'timebase_unit':
                '''Unit of the quantity the profiler sampled on: the session's
                   primary (timebase) counter (e.g. "ns", "cycles",
                   "instructions", "count"). NULL if unknown.''',
            'cmdline':
                '''Command line used to collect the data.''',
        }))

PROFILER_COUNTER_SET_TABLE = Table(
    python_module=__file__,
    class_name='ProfilerCounterSetTable',
    sql_name='__intrinsic_profiler_counter_set',
    columns=[
        C(
            'counter_set_id',
            CppUint32(),
            flags=ColumnFlag.SORTED | ColumnFlag.SET_ID,
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'counter_id',
            CppTableId(COUNTER_TABLE),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''Associates counter values with profiler samples via set IDs.
               Each set contains the counter values (timebase and followers)
               recorded at a single sample point.''',
        group='Callstack profilers',
        columns={
            'counter_set_id':
                '''Set ID that groups counter values for a single sample.
                   Multiple rows share the same ID to form a set.''',
            'counter_id':
                '''Reference to the counter value in the counter table.''',
        }))

PROFILER_SAMPLE_TABLE = Table(
    python_module=__file__,
    class_name='ProfilerSampleTable',
    sql_name='__intrinsic_profiler_sample',
    columns=[
        C(
            'ts',
            CppInt64(),
            flags=ColumnFlag.SORTED,
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('source', CppString()),
        C(
            'task_context_id',
            CppOptional(CppTableId(PROFILER_TASK_CONTEXT_TABLE)),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'execution_context_id',
            CppOptional(CppTableId(PROFILER_EXECUTION_CONTEXT_TABLE)),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'callsite_id',
            CppOptional(CppTableId(STACK_PROFILE_CALLSITE_TABLE)),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('unwind_error', CppOptional(CppString())),
        C('session_id', CppOptional(CppTableId(PROFILER_SESSION_TABLE))),
        C(
            'counter_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
            sql_access=SqlAccess.HIGH_PERF,
        ),
    ],
    tabledoc=TableDoc(
        doc='''The generic sampler table: one row per sample from any
               profiler source (linux perf, chrome, macOS instruments, the
               StackSample packet, ...). A sample usually carries a callstack;
               samples without one (counter-only samples, or samples whose
               stack could not be unwound) have a null callsite_id. Counter
               values recorded at the sample point are linked via
               counter_set_id. Public per-source views (perf_sample, ...) and
               the stack_sample view are defined over this table.''',
        group='Callstack profilers',
        columns={
            'ts':
                '''Timestamp of the sample.''',
            'source':
                '''The profiler that produced the sample (e.g. "linux.perf",
                   "chrome", "instruments").''',
            'task_context_id':
                '''The process, thread and/or stackful asynchronous context
                   this sample is attributed to.''',
            'execution_context_id':
                '''The CPU and privilege mode in which this sample was
                   captured, if known.''',
            'callsite_id':
                '''If set, the captured callstack.''',
            'unwind_error':
                '''If set, indicates that the unwinding for this sample
                   encountered an error. Such samples can still reference a
                   best-effort callstack via callsite_id, with a synthetic
                   error frame at the point where unwinding stopped.''',
            'session_id':
                '''The profiler session (data source instance) this sample
                   came from. Distinguishes samples from concurrent sampling
                   streams.''',
            'counter_set_id':
                '''References the set of counter values recorded at this
                   sample point in __intrinsic_profiler_counter_set.''',
        }))

CHROME_STACK_SAMPLE_EXTRAS_TABLE = Table(
    python_module=__file__,
    class_name='ChromeStackSampleExtrasTable',
    sql_name='__intrinsic_chrome_stack_sample_extras',
    columns=[
        C(
            'profiler_sample_id',
            CppTableId(PROFILER_SAMPLE_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'process_priority',
            CppInt32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''Chrome-specific attributes of a profiler sample. One row per
               chrome streaming profile sample with a non-default process
               priority.''',
        group='Callstack profilers',
        columns={
            'profiler_sample_id':
                '''The profiler sample these attributes belong to.''',
            'process_priority':
                '''Priority of the process when the sample was taken.''',
        }))

HEAP_GRAPH_TABLE = Table(
    python_module=__file__,
    class_name='HeapGraphTable',
    sql_name='__intrinsic_heap_graph',
    wrapping_sql_view=WrappingSqlView('heap_graph'),
    columns=[
        C(
            'ts',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'upid',
            CppUint32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'dump_reason',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'heap_size',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'truncated',
            CppBool(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    tabledoc=TableDoc(
        doc='A list of heap graphs (heap dumps) captured during the trace.',
        group='Callstack profilers',
        columns={
            'ts':
                'Timestamp of the heap dump in nanoseconds.',
            'upid':
                'Unique ID of the process whose heap was dumped. Joinable with process.upid.',
            'dump_reason':
                'Reason why the heap graph was dumped (e.g. OOME, periodic, manual).',
            'heap_size':
                'Total bytes allocated in the heap as reported by the VM.',
            'truncated':
                'True if the heap dump was incomplete (missing packets).',
        }),
)

HEAP_GRAPH_THREAD_CALLSITE_TABLE = Table(
    python_module=__file__,
    class_name='HeapGraphThreadCallsiteTable',
    sql_name='__intrinsic_heap_graph_thread_callsite',
    wrapping_sql_view=WrappingSqlView('heap_graph_thread_callsite'),
    columns=[
        C(
            'heap_graph_id',
            CppTableId(HEAP_GRAPH_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'utid',
            CppUint32(),
        ),
        C(
            'callsite_id',
            CppOptional(CppTableId(STACK_PROFILE_CALLSITE_TABLE)),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='Callstack profiles of threads at the time the heap graph was collected.',
        group='Callstack profilers',
        columns={
            'heap_graph_id':
                'The heap graph instance. Joinable with heap_graph.id.',
            'utid':
                'The thread ID. Joinable with thread.utid.',
            'callsite_id':
                '''The callsite of the leaf frame of the stacktrace.
                              Joinable with stack_profile_callsite.id''',
        }),
)

HEAP_GRAPH_JAVA_OOME_DETAILS_TABLE = Table(
    python_module=__file__,
    class_name='HeapGraphJavaOomeDetailsTable',
    sql_name='__intrinsic_heap_graph_java_oome_details',
    wrapping_sql_view=WrappingSqlView('android_heap_graph_java_oome_details'),
    columns=[
        C(
            'heap_graph_id',
            CppTableId(HEAP_GRAPH_TABLE),
        ),
        C('allocation_size_bytes', CppInt64()),
        C('total_bytes_free', CppInt64()),
        C('free_bytes_until_oom', CppInt64()),
        C('error_msg', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='Details of Java OutOfMemoryError exceptions that triggered heap dumps.',
        group='Callstack profilers',
        columns={
            'heap_graph_id':
                'The heap graph instance this OOM trigger details belongs to. Joinable with heap_graph.id.',
            'allocation_size_bytes':
                'Number of bytes that triggered the OOME.',
            'total_bytes_free':
                'Total free bytes in the Java heap at OOME time.',
            'free_bytes_until_oom':
                'Free bytes remaining until OOME.',
            'error_msg':
                'Error message associated with the OOME exception.',
        }),
)

SYMBOL_TABLE = Table(
    python_module=__file__,
    class_name='SymbolTable',
    sql_name='__intrinsic_stack_profile_symbol',
    wrapping_sql_view=WrappingSqlView('stack_profile_symbol'),
    columns=[
        C(
            'symbol_set_id',
            CppUint32(),
            flags=ColumnFlag.SORTED | ColumnFlag.SET_ID,
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
            'source_file',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'line_number',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'inlined',
            CppOptional(CppBool()),
        ),
    ],
    tabledoc=TableDoc(
        doc='''
            Symbolization data for a frame. Rows with the same symbol_set_id
            describe one callframe, with the most-inlined symbol having
            id == symbol_set_id.

            For instance, if the function foo has an inlined call to the
            function bar, which has an inlined call to baz, the
            stack_profile_symbol table would look like this.

            ```
            |id|symbol_set_id|name         |source_file|line_number|
            |--|-------------|-------------|-----------|-----------|
            |1 |      1      |baz          |foo.cc     | 36        |
            |2 |      1      |bar          |foo.cc     | 30        |
            |3 |      1      |foo          |foo.cc     | 60        |
            ```
        ''',
        group='Callstack profilers',
        columns={
            'name':
                '''name of the function.''',
            'source_file':
                '''name of the source file containing the function.''',
            'line_number':
                '''
                    line number of the frame in the source file. This is the
                    exact line for the corresponding program counter, not the
                    beginning of the function.
                ''',
            'inlined':
                '''
                    whether this function was inlined into another function.
                    True for inlined functions, false for non-inlined functions,
                    null if inlining information is not available.
                ''',
            'symbol_set_id':
                ''''''
        }))

HEAP_PROFILE_TABLE = Table(
    python_module=__file__,
    class_name='HeapProfileTable',
    sql_name='__intrinsic_heap_profile',
    wrapping_sql_view=WrappingSqlView('heap_profile'),
    columns=[
        C('ts', CppInt64()),
        C('ts_end', CppInt64()),
        C('dur', CppInt64()),
        C('upid', CppUint32()),
        C('heap_name', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='''
          A list of heap profiles (heapprofd dumps) captured during the trace.
          Each row describes the profiling window a single dump represents for a
          single heap (e.g. the native "libc.malloc" heap or an ART heap).
        ''',
        group='Callstack profilers',
        columns={
            'ts':
                '''Timestamp of the start of the profiling window in
                nanoseconds.''',
            'ts_end':
                '''Timestamp of the end of the profiling window (i.e. when the
                dump was taken) in nanoseconds. This is the timestamp the
                allocations are recorded at, so heap_profile_allocation joins
                this table via (upid, heap_profile_allocation.ts = ts_end).''',
            'dur':
                '''Duration of the profiling window in nanoseconds
                (ts_end - ts).''',
            'upid':
                '''Unique ID of the process whose heap was dumped. Joinable with
                process.upid.''',
            'heap_name':
                '''Name of the heap this dump is for (e.g. "libc.malloc" for the
                native heap), or NULL if the producer did not report one.''',
        }),
)

HEAP_PROFILE_ALLOCATION_TABLE = Table(
    python_module=__file__,
    class_name='HeapProfileAllocationTable',
    sql_name='__intrinsic_heap_profile_allocation',
    wrapping_sql_view=WrappingSqlView('heap_profile_allocation'),
    columns=[
        # TODO(b/193757386): readd the sorted flag once this bug is fixed.
        C(
            'ts',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'upid',
            CppUint32(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('heap_name', CppString()),
        C(
            'callsite_id',
            CppTableId(STACK_PROFILE_CALLSITE_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'count',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'size',
            CppInt64(),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          Allocations that happened at a callsite.

          NOTE: this table is not sorted by timestamp intentionanlly -
          see b/193757386 for details.

          This is generated by heapprofd.
        ''',
        group='Callstack profilers',
        columns={
            'ts':
                '''The timestamp the allocations happened at. heapprofd batches
                allocations and frees, and all data from a dump will have the
                same timestamp. This is the end of the dump's profiling window,
                so it is joinable with heap_profile via
                (upid, ts = heap_profile.ts_end).''',
            'upid':
                '''The unique PID of the allocating process.''',
            'callsite_id':
                '''The callsite the allocation happened at.''',
            'count':
                '''If positive: number of allocations that happened at this
                callsite. if negative: number of allocations that happened at
                this callsite that were freed.''',
            'size':
                '''If positive: size of allocations that happened at this
                callsite. if negative: size of allocations that happened at this
                callsite that were freed.''',
            'heap_name':
                ''''''
        }))

HEAP_GRAPH_CLASS_TABLE = Table(
    python_module=__file__,
    class_name='HeapGraphClassTable',
    sql_name='__intrinsic_heap_graph_class',
    columns=[
        C(
            'name',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'deobfuscated_name',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'location',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'superclass_id',
            CppOptional(CppSelfTableId()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        # classloader_id should really be HeapGraphObject::id, but that
        # would create a loop, which is currently not possible.
        # TODO(lalitm): resolve this
        C(
            'classloader_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'kind',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''''',
        group='ART Heap Graphs',
        columns={
            'name':
                '''(potentially obfuscated) name of the class.''',
            'deobfuscated_name':
                '''if class name was obfuscated and deobfuscation map
                for it provided, the deobfuscated name.''',
            'location':
                '''the APK / Dex / JAR file the class is contained in.
            ''',
            'superclass_id':
                '''''',
            'classloader_id':
                '''''',
            'kind':
                ''''''
        }))

HEAP_GRAPH_OBJECT_TABLE = Table(
    python_module=__file__,
    class_name='HeapGraphObjectTable',
    sql_name='__intrinsic_heap_graph_object',
    columns=[
        C('upid', CppUint32()),
        C('graph_sample_ts', CppInt64(), cpp_access=CppAccess.READ),
        C(
            'self_size',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'native_size',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'reference_set_id',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            cpp_access=CppAccess.READ_AND_HIGH_PERF_WRITE,
            flags=ColumnFlag.DENSE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'reachable',
            CppInt32(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'heap_type',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'type_id',
            CppTableId(HEAP_GRAPH_CLASS_TABLE),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'root_type',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'root_distance',
            CppInt32(),
            flags=ColumnFlag.HIDDEN,
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'object_data_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          The objects on the Dalvik heap.

          All rows with the same (upid, graph_sample_ts) are one dump.
        ''',
        group='ART Heap Graphs',
        columns={
            'upid':
                '''Unique PID of the target.''',
            'graph_sample_ts':
                '''timestamp this dump was taken at.''',
            'self_size':
                '''size this object uses on the Java Heap.''',
            'native_size':
                '''approximate amount of native memory used by this object,
                as reported by libcore.util.NativeAllocationRegistry.size.''',
            'reference_set_id':
                '''join key with heap_graph_reference containing all
                objects referred in this object's fields.''',
            'reachable':
                '''bool whether this object is reachable from a GC root. If
                false, this object is uncollected garbage.''',
            'heap_type':
                '''The type of ART heap this object is stored on (app, zygote,
                boot image)''',
            'type_id':
                '''class this object is an instance of.''',
            'root_type':
                '''if not NULL, this object is a GC root.''',
            'root_distance':
                '''''',
            'object_data_id':
                '''optional ID into heap_graph_object_data for HPROF
                primitive field values and array data.''',
        }))

HEAP_GRAPH_OBJECT_DATA_TABLE = Table(
    python_module=__file__,
    class_name='HeapGraphObjectDataTable',
    sql_name='__intrinsic_heap_graph_object_data',
    columns=[
        C(
            'field_set_id',
            CppOptional(CppUint32()),
            sql_access=SqlAccess.HIGH_PERF,
            flags=ColumnFlag.DENSE,
        ),
        C(
            'value_string',
            CppOptional(CppString()),
        ),
        C(
            'array_element_type',
            CppOptional(CppString()),
        ),
        C(
            'array_element_count',
            CppOptional(CppUint32()),
        ),
        C(
            'array_data_id',
            CppOptional(CppUint32()),
        ),
        C(
            'array_data_hash',
            CppOptional(CppInt64()),
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          HPROF-specific data for heap graph objects.

          Contains decoded string content and primitive array blob
          references. Only populated for HPROF (ART) heap dumps, not
          for proto heap graphs.
        ''',
        group='ART Heap Graphs',
        columns={
            'field_set_id':
                '''join key with heap_graph_primitive containing
                primitive field values for this object.''',
            'value_string':
                '''decoded string value for java.lang.String
                instances.''',
            'array_element_type':
                '''for primitive array objects, the element type
                (boolean, byte, char, short, int, long, float, double).''',
            'array_element_count':
                '''for primitive array objects, the number of elements.''',
            'array_data_id':
                '''for primitive array objects, opaque ID to retrieve
                the raw element bytes via
                __intrinsic_heap_graph_array() or its JSON
                representation via
                __intrinsic_heap_graph_array_json().''',
            'array_data_hash':
                '''for primitive array objects, a 64-bit content hash
                of the raw element bytes. Two arrays with the same
                hash have identical content.'''
        }))

HEAP_GRAPH_REFERENCE_TABLE = Table(
    python_module=__file__,
    class_name='HeapGraphReferenceTable',
    sql_name='__intrinsic_heap_graph_reference',
    columns=[
        C(
            'reference_set_id',
            CppUint32(),
            flags=ColumnFlag.SORTED | ColumnFlag.SET_ID,
        ),
        C(
            'owner_id',
            CppTableId(HEAP_GRAPH_OBJECT_TABLE),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'owned_id',
            CppOptional(CppTableId(HEAP_GRAPH_OBJECT_TABLE)),
            cpp_access=CppAccess.READ,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'field_name',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'field_type_name',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'deobfuscated_field_name',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          Many-to-many mapping between heap_graph_object.

          This associates the object with given reference_set_id with the
          objects that are referred to by its fields.
        ''',
        group='ART Heap Graphs',
        columns={
            'reference_set_id':
                '''Join key to heap_graph_object.''',
            'owner_id':
                '''Id of object that has this reference_set_id.''',
            'owned_id':
                '''Id of object that is referred to.''',
            'field_name':
                '''The field that refers to the object. E.g. Foo.name.''',
            'field_type_name':
                '''The static type of the field. E.g. java.lang.String.''',
            'deobfuscated_field_name':
                '''The deobfuscated name, if field_name was obfuscated and a
                deobfuscation mapping was provided for it.'''
        }))

HEAP_GRAPH_PRIMITIVE_TABLE = Table(
    python_module=__file__,
    class_name='HeapGraphPrimitiveTable',
    sql_name='__intrinsic_heap_graph_primitive',
    columns=[
        C(
            'field_set_id',
            CppUint32(),
            flags=ColumnFlag.SORTED | ColumnFlag.SET_ID,
        ),
        C(
            'field_name',
            CppString(),
        ),
        C(
            'field_type',
            CppString(),
        ),
        C(
            'bool_value',
            CppOptional(CppUint32()),
        ),
        C(
            'byte_value',
            CppOptional(CppInt64()),
        ),
        C(
            'char_value',
            CppOptional(CppInt64()),
        ),
        C(
            'short_value',
            CppOptional(CppInt64()),
        ),
        C(
            'int_value',
            CppOptional(CppInt64()),
        ),
        C(
            'long_value',
            CppOptional(CppInt64()),
        ),
        C(
            'float_value',
            CppOptional(CppDouble()),
        ),
        C(
            'double_value',
            CppOptional(CppDouble()),
        ),
    ],
    tabledoc=TableDoc(
        doc='''
          Primitive field values for heap graph objects.

          This associates the object with given field_set_id with its
          primitive field values (for instances).
        ''',
        group='ART Heap Graphs',
        columns={
            'field_set_id':
                '''Join key to heap_graph_object_data.field_set_id.''',
            'field_name':
                '''The field name. E.g. Foo.count.''',
            'field_type':
                '''The primitive type. E.g. int, boolean, float.''',
            'bool_value':
                '''Value for boolean fields (0 or 1).''',
            'byte_value':
                '''Value for byte fields.''',
            'char_value':
                '''Value for char fields (as integer codepoint).''',
            'short_value':
                '''Value for short fields.''',
            'int_value':
                '''Value for int fields.''',
            'long_value':
                '''Value for long fields.''',
            'float_value':
                '''Value for float fields.''',
            'double_value':
                '''Value for double fields.''',
        }))

AGGREGATE_PROFILE_TABLE = Table(
    python_module=__file__,
    class_name='AggregateProfileTable',
    sql_name='__intrinsic_aggregate_profile',
    columns=[
        C('scope', CppString()),
        C('name', CppString()),
        C('sample_type_type', CppString()),
        C('sample_type_unit', CppString()),
    ],
    tabledoc=TableDoc(
        doc='''
          Represents a single metric from a pprof file.
          This is generated by the pprof importer.
        ''',
        group='Callstack profilers',
        columns={
            'scope':
                '''Filename or identifier (e.g., "app.pprof").''',
            'name':
                '''Human-readable name (e.g., "CPU Samples").''',
            'sample_type_type':
                '''From pprof ValueType.type (e.g., "cpu", "allocations").''',
            'sample_type_unit':
                '''From pprof ValueType.unit (e.g., "nanoseconds", "count").'''
        }))

AGGREGATE_SAMPLE_TABLE = Table(
    python_module=__file__,
    class_name='AggregateSampleTable',
    sql_name='__intrinsic_aggregate_sample',
    columns=[
        C('aggregate_profile_id', CppTableId(AGGREGATE_PROFILE_TABLE)),
        C('callsite_id', CppTableId(STACK_PROFILE_CALLSITE_TABLE)),
        C('value', CppDouble()),
    ],
    tabledoc=TableDoc(
        doc='''
          Individual sample values for each callsite in pprof data.
          This is generated by the pprof importer.
        ''',
        group='Callstack profilers',
        columns={
            'aggregate_profile_id': '''FK to aggregate_profile.''',
            'callsite_id': '''FK to stack_profile_callsite.''',
            'value': '''Sample value.'''
        }))

VULKAN_MEMORY_ALLOCATIONS_TABLE = Table(
    python_module=__file__,
    class_name='VulkanMemoryAllocationsTable',
    sql_name='__intrinsic_vulkan_memory_allocations',
    wrapping_sql_view=WrappingSqlView('vulkan_memory_allocations'),
    columns=[
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C('source', CppString()),
        C('operation', CppString()),
        C('timestamp', CppInt64()),
        C('upid', CppOptional(CppUint32())),
        C('device', CppOptional(CppInt64())),
        C('device_memory', CppOptional(CppInt64())),
        C('memory_type', CppOptional(CppUint32())),
        C('heap', CppOptional(CppUint32())),
        C('function_name', CppOptional(CppString())),
        C('object_handle', CppOptional(CppInt64())),
        C('memory_address', CppOptional(CppInt64())),
        C('memory_size', CppOptional(CppInt64())),
        C('scope', CppString()),
    ],
    tabledoc=TableDoc(
        doc='''''',
        group='Misc',
        columns={
            'arg_set_id': '''''',
            'source': '''''',
            'operation': '''''',
            'timestamp': '''''',
            'upid': '''''',
            'device': '''''',
            'device_memory': '''''',
            'memory_type': '''''',
            'heap': '''''',
            'function_name': '''''',
            'object_handle': '''''',
            'memory_address': '''''',
            'memory_size': '''''',
            'scope': ''''''
        }))

GPU_COUNTER_GROUP_TABLE = Table(
    python_module=__file__,
    class_name='GpuCounterGroupTable',
    sql_name='__intrinsic_gpu_counter_group',
    wrapping_sql_view=WrappingSqlView('gpu_counter_group'),
    columns=[
        C('group_id', CppInt32()),
        C('track_id', CppTableId(TRACK_TABLE)),
        C('name', CppOptional(CppString())),
        C('description', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='''Maps GPU counter tracks to groups.''',
        group='Misc',
        columns={
            'group_id':
                '''Group identifier (enum value for legacy groups, custom
                ID for producer-defined groups).''',
            'track_id':
                '''Track table reference for the counter.''',
            'name':
                '''Group name. NULL for legacy enum-based groups.''',
            'description':
                '''Group description. NULL for legacy enum-based groups.''',
        }))

GPU_CONTEXT_TABLE = Table(
    python_module=__file__,
    class_name='GpuContextTable',
    sql_name='__intrinsic_gpu_context',
    wrapping_sql_view=WrappingSqlView('gpu_context'),
    columns=[
        C('context_id', CppUint32()),
        C('pid', CppOptional(CppUint32())),
        C('api', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='''Maps GPU graphics context IDs to process and API type.

Each row represents a unique graphics context seen in the trace,
populated from InternedGraphicsContext data attached to
GpuRenderStageEvent packets.''',
        group='Misc',
        columns={
            'context_id':
                '''The graphics context handle (GL context, VkDevice,
                CUDA context, etc.).''',
            'pid':
                '''Process ID associated with this context.''',
            'api':
                '''Graphics API type (OPEN_GL, VULKAN, OPEN_CL, CUDA,
                HIP, or UNDEFINED).''',
        }))

# TODO(lalitm): delete this once we have proper tree functions.
EXPERIMENTAL_FLAMEGRAPH_TABLE = Table(
    python_module=__file__,
    class_name='ExperimentalFlamegraphTable',
    purpose=Purpose.STATIC_TABLE_FUNCTION,
    sql_name='experimental_flamegraph',
    columns=[
        C(
            'profile_type',
            CppString(),
            flags=ColumnFlag.HIDDEN,
            cpp_access=CppAccess.READ,
        ),
        C(
            'ts_in',
            CppOptional(CppInt64()),
            flags=ColumnFlag.SORTED | ColumnFlag.HIDDEN,
        ),
        C('ts_constraint', CppOptional(CppString()), flags=ColumnFlag.HIDDEN),
        C(
            'upid',
            CppOptional(CppUint32()),
            flags=ColumnFlag.HIDDEN,
            cpp_access=CppAccess.READ,
        ),
        C('upid_group', CppOptional(CppString()), flags=ColumnFlag.HIDDEN),
        C('focus_str', CppOptional(CppString()), flags=ColumnFlag.HIDDEN),
        C(
            'ts',
            CppInt64(),
            flags=ColumnFlag.SORTED,
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C('depth', CppUint32(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C('name', CppString(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C(
            'map_name',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C('count', CppInt64(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C(
            'cumulative_count',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C('size', CppInt64(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C(
            'cumulative_size',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'alloc_count',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'cumulative_alloc_count',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'alloc_size',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'cumulative_alloc_size',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'parent_id',
            CppOptional(CppSelfTableId()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'source_file',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'line_number',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    tabledoc=TableDoc(
        doc='''
            Table used to render flamegraphs. This gives cumulative sizes of
            nodes in the flamegraph.

            WARNING: This is experimental and the API is subject to change.
        ''',
        group='Callstack profilers',
        columns={
            'ts': '''''',
            'upid': '''''',
            'profile_type': '''''',
            'focus_str': '''''',
            'depth': '''''',
            'name': '''''',
            'map_name': '''''',
            'count': '''''',
            'cumulative_count': '''''',
            'size': '''''',
            'cumulative_size': '''''',
            'alloc_count': '''''',
            'cumulative_alloc_count': '''''',
            'alloc_size': '''''',
            'cumulative_alloc_size': '''''',
            'parent_id': '''''',
            'source_file': '''''',
            'line_number': '''''',
            'upid_group': ''''''
        }))

# Keep this list sorted.
ALL_TABLES = [
    AGGREGATE_PROFILE_TABLE,
    AGGREGATE_SAMPLE_TABLE,
    CHROME_STACK_SAMPLE_EXTRAS_TABLE,
    EXPERIMENTAL_FLAMEGRAPH_TABLE,
    GPU_CONTEXT_TABLE,
    GPU_COUNTER_GROUP_TABLE,
    HEAP_GRAPH_CLASS_TABLE,
    HEAP_GRAPH_JAVA_OOME_DETAILS_TABLE,
    HEAP_GRAPH_OBJECT_DATA_TABLE,
    HEAP_GRAPH_PRIMITIVE_TABLE,
    HEAP_GRAPH_OBJECT_TABLE,
    HEAP_GRAPH_REFERENCE_TABLE,
    HEAP_GRAPH_TABLE,
    HEAP_GRAPH_THREAD_CALLSITE_TABLE,
    HEAP_PROFILE_TABLE,
    HEAP_PROFILE_ALLOCATION_TABLE,
    PACKAGE_LIST_TABLE,
    PROFILER_ASYNC_CONTEXT_TABLE,
    PROFILER_COUNTER_SET_TABLE,
    PROFILER_EXECUTION_CONTEXT_TABLE,
    PROFILER_SAMPLE_TABLE,
    PROFILER_SESSION_TABLE,
    PROFILER_SMAPS_TABLE,
    PROFILER_TASK_CONTEXT_TABLE,
    STACK_PROFILE_CALLSITE_TABLE,
    STACK_PROFILE_FRAME_TABLE,
    STACK_PROFILE_MAPPING_TABLE,
    SYMBOL_TABLE,
    VULKAN_MEMORY_ALLOCATIONS_TABLE,
]
