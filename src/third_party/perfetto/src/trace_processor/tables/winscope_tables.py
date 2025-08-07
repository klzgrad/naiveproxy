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

from python.generators.trace_processor_table.public import Column as C
from python.generators.trace_processor_table.public import ColumnFlag
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import CppTableId
from python.generators.trace_processor_table.public import CppUint32
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc
from python.generators.trace_processor_table.public import WrappingSqlView
from python.generators.trace_processor_table.public import CppDouble

WINSCOPE_RECT_TABLE = Table(
    python_module=__file__,
    class_name='WinscopeRectTable',
    sql_name='__intrinsic_winscope_rect',
    columns=[
        C('x', CppDouble()),
        C('y', CppDouble()),
        C('w', CppDouble()),
        C('h', CppDouble()),
    ],
    tabledoc=TableDoc(
        doc='WinscopeRect',
        group='Winscope',
        columns={
            'x': 'X position of rect',
            'y': 'Y position of rect',
            'w': 'Width of rect',
            'h': 'Height of rect',
        }))

WINSCOPE_TRANSFORM_TABLE = Table(
    python_module=__file__,
    class_name='WinscopeTransformTable',
    sql_name='__intrinsic_winscope_transform',
    columns=[
        C('dsdx', CppDouble()),
        C('dtdx', CppDouble()),
        C('tx', CppDouble()),
        C('dtdy', CppDouble()),
        C('dsdy', CppDouble()),
        C('ty', CppDouble()),
    ],
    tabledoc=TableDoc(
        doc='WinscopeTransform',
        group='Winscope',
        columns={
            'dsdx': 'Dsdx',
            'dtdx': 'Dtdx',
            'tx': 'Tx',
            'dtdy': 'Dtdy',
            'dsdy': 'Dsdy',
            'ty': 'Ty',
        }))

WINSCOPE_TRACE_RECT_TABLE = Table(
    python_module=__file__,
    class_name='WinscopeTraceRectTable',
    sql_name='__intrinsic_winscope_trace_rect',
    columns=[
        C('rect_id', CppTableId(WINSCOPE_RECT_TABLE)),
        C('group_id', CppUint32()),
        C('depth', CppUint32()),
        C('is_spy', CppInt64()),
        C('is_visible', CppInt64()),
        C('opacity', CppOptional(CppDouble())),
        C('transform_id', CppTableId(WINSCOPE_TRANSFORM_TABLE)),
    ],
    tabledoc=TableDoc(
        doc='WinscopeTraceRect',
        group='Winscope',
        columns={
            'trace_rect_id':
                'Used to associate rect with row in Winscope trace table',
            'rect_id':
                'Used to match trace rect to rect in __intrinsic_winscope_rect',
            'group_id':
                'Group id',
            'depth':
                'Depth',
            'is_visible':
                'Is visible rect',
            'is_spy':
                'Is spy window (for input windows)',
            'opacity':
                'Opacity',
            'transform_id':
                'Used to match trace rect to transform in __intrinsic_winscope_transform',
        }))

INPUTMETHOD_CLIENTS_TABLE = Table(
    python_module=__file__,
    class_name='InputMethodClientsTable',
    sql_name='__intrinsic_inputmethod_clients',
    columns=[
        C('ts', CppInt64(), ColumnFlag.SORTED),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='InputMethod clients',
        group='Winscope',
        columns={
            'ts': 'The timestamp the dump was triggered',
            'arg_set_id': 'Extra args parsed from the proto message',
            'base64_proto_id': 'String id for raw proto message',
        }))

INPUTMETHOD_MANAGER_SERVICE_TABLE = Table(
    python_module=__file__,
    class_name='InputMethodManagerServiceTable',
    sql_name='__intrinsic_inputmethod_manager_service',
    columns=[
        C('ts', CppInt64(), ColumnFlag.SORTED),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='InputMethod manager service',
        group='Winscope',
        columns={
            'ts': 'The timestamp the dump was triggered',
            'arg_set_id': 'Extra args parsed from the proto message',
            'base64_proto_id': 'String id for raw proto message',
        }))

INPUTMETHOD_SERVICE_TABLE = Table(
    python_module=__file__,
    class_name='InputMethodServiceTable',
    sql_name='__intrinsic_inputmethod_service',
    columns=[
        C('ts', CppInt64(), ColumnFlag.SORTED),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='InputMethod service',
        group='Winscope',
        columns={
            'ts': 'The timestamp the dump was triggered',
            'arg_set_id': 'Extra args parsed from the proto message',
            'base64_proto_id': 'String id for raw proto message',
        }))

SURFACE_FLINGER_LAYERS_SNAPSHOT_TABLE = Table(
    python_module=__file__,
    class_name='SurfaceFlingerLayersSnapshotTable',
    sql_name='surfaceflinger_layers_snapshot',
    columns=[
        C('ts', CppInt64(), ColumnFlag.SORTED),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('sequence_id', CppUint32())
    ],
    tabledoc=TableDoc(
        doc='SurfaceFlinger layers snapshot',
        group='Winscope',
        columns={
            'ts': 'Timestamp of the snapshot',
            'arg_set_id': 'Extra args parsed from the proto message',
            'base64_proto_id': 'String id for raw proto message',
            'sequence_id': 'Sequence id of the trace packet'
        }))

SURFACE_FLINGER_DISPLAY_TABLE = Table(
    python_module=__file__,
    class_name='SurfaceFlingerDisplayTable',
    sql_name='__intrinsic_surfaceflinger_display',
    columns=[
        C('snapshot_id', CppTableId(SURFACE_FLINGER_LAYERS_SNAPSHOT_TABLE)),
        C('is_on', CppInt64()),
        C('is_virtual', CppInt64()),
        C('trace_rect_id', CppTableId(WINSCOPE_TRACE_RECT_TABLE)),
        C('display_id', CppInt64()),
        C('display_name', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='SurfaceFlinger display',
        group='Winscope',
        columns={
            'snapshot_id':
                'The snapshot that generated this display',
            'is_on':
                'Display is on',
            'is_virtual':
                'Display is virtual',
            'trace_rect_id':
                'Used to associate with row in __intrinsic_winscope_trace_rect',
            'display_id':
                'Display id',
            'display_name':
                'Display name'
        }))

SURFACE_FLINGER_LAYER_TABLE = Table(
    python_module=__file__,
    class_name='SurfaceFlingerLayerTable',
    sql_name='surfaceflinger_layer',
    columns=[
        C('snapshot_id', CppTableId(SURFACE_FLINGER_LAYERS_SNAPSHOT_TABLE)),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('layer_id', CppInt64()),
        C('layer_name', CppString()),
        C('is_visible', CppInt64()),
        C('parent', CppOptional(CppInt64())),
        C('corner_radius', CppOptional(CppDouble())),
        C('hwc_composition_type', CppOptional(CppInt64())),
        C('is_hidden_by_policy', CppInt64()),
        C('z_order_relative_of', CppOptional(CppInt64())),
        C('is_missing_z_parent', CppInt64()),
        C('layer_rect_id', CppOptional(CppTableId(WINSCOPE_TRACE_RECT_TABLE))),
        C('input_rect_id', CppOptional(CppTableId(WINSCOPE_TRACE_RECT_TABLE)))
    ],
    tabledoc=TableDoc(
        doc='SurfaceFlinger layer',
        group='Winscope',
        columns={
            'snapshot_id':
                'The snapshot that generated this layer',
            'arg_set_id':
                'Extra args parsed from the proto message',
            'base64_proto_id':
                'String id for raw proto message',
            'layer_id':
                'Layer id',
            'layer_name':
                'Layer name',
            'is_visible':
                'Computed layer visibility',
            'parent':
                'Parent layer id',
            'corner_radius':
                'Layer corner radius',
            'hwc_composition_type':
                'Hwc composition type',
            'is_hidden_by_policy':
                'Is hidden by policy',
            'z_order_relative_of':
                'Z parent',
            'is_missing_z_parent':
                'Is Z parent missing',
            'layer_rect_id':
                'Used to associate with row in __intrinsic_winscope_trace_rect',
            'input_rect_id':
                'Used to associate with row in __intrinsic_winscope_trace_rect',
        }))

WINSCOPE_FILL_REGION_TABLE = Table(
    python_module=__file__,
    class_name='WinscopeFillRegionTable',
    sql_name='__intrinsic_winscope_fill_region',
    columns=[
        C('trace_rect_id', CppTableId(WINSCOPE_TRACE_RECT_TABLE)),
        C('rect_id', CppTableId(WINSCOPE_RECT_TABLE)),
    ],
    tabledoc=TableDoc(
        doc='WinscopeFillRegion',
        group='Winscope',
        columns={
            'trace_rect_id':
                'Used to associate row in __intrinsic_winscope_trace_rect with fill region',
            'rect_id':
                'Used to associate region with row in __intrinsic_winscope_rect',
        }))

SURFACE_FLINGER_TRANSACTIONS_TABLE = Table(
    python_module=__file__,
    class_name='SurfaceFlingerTransactionsTable',
    sql_name='surfaceflinger_transactions',
    columns=[
        C('ts', CppInt64(), ColumnFlag.SORTED),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('vsync_id', CppOptional(CppInt64())),
    ],
    tabledoc=TableDoc(
        doc='SurfaceFlinger transactions. Each row contains a set of ' +
        'transactions that SurfaceFlinger committed together.',
        group='Winscope',
        columns={
            'ts': 'Timestamp of the transactions commit',
            'arg_set_id': 'Extra args parsed from the proto message',
            'base64_proto_id': 'String id for raw proto message',
            'vsync_id': 'Vsync id taken from raw proto message',
        }))

SURFACE_FLINGER_TRANSACTION_TABLE = Table(
    python_module=__file__,
    class_name='SurfaceFlingerTransactionTable',
    sql_name='__intrinsic_surfaceflinger_transaction',
    columns=[
        C('snapshot_id', CppTableId(SURFACE_FLINGER_TRANSACTIONS_TABLE)),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('transaction_id', CppOptional(CppInt64())),
        C('pid', CppOptional(CppUint32())),
        C('uid', CppOptional(CppUint32())),
        C('layer_id', CppOptional(CppUint32())),
        C('display_id', CppOptional(CppUint32())),
        C('flags_id', CppOptional(CppUint32())),
        C('transaction_type', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='SurfaceFlinger transaction',
        group='Winscope',
        columns={
            'snapshot_id':
                'The snapshot that generated this transaction',
            'arg_set_id':
                'Extra args parsed from the proto message',
            'base64_proto_id':
                'String id for raw proto message',
            'transaction_id':
                'Transaction id taken from raw proto message',
            'pid':
                'Pid taken from raw proto message',
            'uid':
                'Uid taken from raw proto message',
            'layer_id':
                'Layer id taken from raw proto message',
            'display_id':
                'Display id taken from raw proto message',
            'flags_id':
                'Flags id used to retrieve associated flags from __intrinsic_surfaceflinger_transaction_flag',
            'transaction_type':
                'Transaction type'
        }))

SURFACE_FLINGER_TRANSACTION_FLAG_TABLE = Table(
    python_module=__file__,
    class_name='SurfaceFlingerTransactionFlagTable',
    sql_name='__intrinsic_surfaceflinger_transaction_flag',
    columns=[
        C('flags_id', CppOptional(CppUint32())),
        C('flag', CppOptional(CppString())),
    ],
    tabledoc=TableDoc(
        doc='SurfaceFlinger transaction flags',
        group='Winscope',
        columns={
            'flags_id':
                'The flags_id corresponding to a row in __intrinsic_surfaceflinger_transaction',
            'flag':
                'The translated flag string',
        }))

VIEWCAPTURE_TABLE = Table(
    python_module=__file__,
    class_name='ViewCaptureTable',
    sql_name='__intrinsic_viewcapture',
    columns=[
        C('ts', CppInt64(), ColumnFlag.SORTED),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='ViewCapture',
        group='Winscope',
        columns={
            'ts': 'The timestamp the views were captured',
            'arg_set_id': 'Extra args parsed from the proto message',
            'base64_proto_id': 'String id for raw proto message',
        }))

VIEWCAPTURE_VIEW_TABLE = Table(
    python_module=__file__,
    class_name='ViewCaptureViewTable',
    sql_name='__intrinsic_viewcapture_view',
    columns=[
        C('snapshot_id', CppTableId(VIEWCAPTURE_TABLE)),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='ViewCapture view',
        group='Winscope',
        columns={
            'snapshot_id': 'The snapshot that generated this view',
            'arg_set_id': 'Extra args parsed from the proto message',
            '': 'String id for raw proto message',
        }))

VIEWCAPTURE_INTERNED_DATA_TABLE = Table(
    python_module=__file__,
    class_name='ViewCaptureInternedDataTable',
    sql_name='__intrinsic_viewcapture_interned_data',
    columns=[
        C(
            'base64_proto_id',
            CppUint32(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C('flat_key', CppString(), cpp_access=CppAccess.READ),
        C('iid', CppInt64(), cpp_access=CppAccess.READ),
        C('deinterned_value', CppString(), cpp_access=CppAccess.READ),
    ],
    tabledoc=TableDoc(
        doc='ViewCapture interned data',
        group='Winscope',
        columns={
            'base64_proto_id': 'String id for raw proto message',
            'flat_key': 'Proto field name',
            'iid': 'Int value set on proto',
            'deinterned_value': 'Corresponding string value',
        }))

WINDOW_MANAGER_SHELL_TRANSITIONS_TABLE = Table(
    python_module=__file__,
    class_name='WindowManagerShellTransitionsTable',
    sql_name='window_manager_shell_transitions',
    columns=[
        C('ts', CppInt64(), cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE),
        C('transition_id', CppInt64(), ColumnFlag.SORTED),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'transition_type',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'send_time_ns',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'dispatch_time_ns',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'duration_ns',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'handler',
            CppOptional(CppInt64()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'status',
            CppOptional(CppString()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'flags',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    tabledoc=TableDoc(
        doc='Window Manager Shell Transitions',
        group='Winscope',
        columns={
            'ts':
                'The timestamp the transition started playing - either dispatch time or send time',
            'transition_id':
                'The id of the transition',
            'arg_set_id':
                'Extra args parsed from the proto message',
            'transition_type':
                'The type of the transition',
            'send_time_ns':
                'Transition send time',
            'dispatch_time_ns':
                'Transition dispatch time',
            'duration_ns':
                'Transition duration',
            'handler':
                'Handler id',
            'status':
                'Transition status',
            'flags':
                'Transition flags',
        }))

WINDOW_MANAGER_SHELL_TRANSITION_HANDLERS_TABLE = Table(
    python_module=__file__,
    class_name='WindowManagerShellTransitionHandlersTable',
    sql_name='window_manager_shell_transition_handlers',
    columns=[
        C('handler_id', CppInt64()),
        C('handler_name', CppString()),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='Window Manager Shell Transition Handlers',
        group='Winscope',
        columns={
            'handler_id': 'The id of the handler',
            'handler_name': 'The name of the handler',
            'base64_proto_id': 'String id for raw proto message',
        }))

WINDOW_MANAGER_SHELL_TRANSITION_PARTICIPANTS_TABLE = Table(
    python_module=__file__,
    class_name='WindowManagerShellTransitionParticipantsTable',
    sql_name='__intrinsic_window_manager_shell_transition_participants',
    columns=[
        C('transition_id', CppInt64()),
        C('layer_id', CppOptional(CppUint32())),
        C('window_id', CppOptional(CppUint32())),
    ],
    tabledoc=TableDoc(
        doc='Window Manager Shell Transition Participants',
        group='Winscope',
        columns={
            'transition_id': 'Transition id',
            'layer_id': 'Id of layer participant',
            'window_id': 'Id of window participant',
        }))

WINDOW_MANAGER_SHELL_TRANSITION_PROTOS_TABLE = Table(
    python_module=__file__,
    class_name='WindowManagerShellTransitionProtosTable',
    sql_name='__intrinsic_window_manager_shell_transition_protos',
    columns=[
        C(
            'transition_id',
            CppInt64(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    tabledoc=TableDoc(
        doc='Window Manager Shell Transition Protos',
        group='Winscope',
        columns={
            'transition_id': 'The id of the transition',
            'base64_proto_id': 'String id for raw proto message',
        }))

WINDOW_MANAGER_TABLE = Table(
    python_module=__file__,
    class_name='WindowManagerTable',
    sql_name='__intrinsic_windowmanager',
    columns=[
        C('ts', CppInt64(), ColumnFlag.SORTED),
        C(
            'arg_set_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'base64_proto_id',
            CppOptional(CppUint32()),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
            cpp_access_duration=CppAccessDuration.POST_FINALIZATION,
        ),
    ],
    wrapping_sql_view=WrappingSqlView('windowmanager'),
    tabledoc=TableDoc(
        doc='WindowManager',
        group='Winscope',
        columns={
            'ts': 'The timestamp the state snapshot was captured',
            'arg_set_id': 'Extra args parsed from the proto message',
            'base64_proto_id': 'String id for raw proto message',
        }))

PROTOLOG_TABLE = Table(
    python_module=__file__,
    class_name='ProtoLogTable',
    sql_name='protolog',
    columns=[
        C(
            'ts',
            CppInt64(),
            ColumnFlag.SORTED,
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'level',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'tag',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'message',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'stacktrace',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
        C(
            'location',
            CppString(),
            cpp_access=CppAccess.READ_AND_LOW_PERF_WRITE,
        ),
    ],
    tabledoc=TableDoc(
        doc='Protolog',
        group='Winscope',
        columns={
            'ts':
                'The timestamp the log message was sent',
            'level':
                'The log level of the protolog message',
            'tag':
                'The log tag of the protolog message',
            'message':
                'The protolog message',
            'stacktrace':
                'Stacktrace captured at the message\'s logpoint',
            'location':
                'The location of the logpoint (only for processed messages)',
        }))

# Keep this list sorted.
ALL_TABLES = [
    WINSCOPE_RECT_TABLE,
    WINSCOPE_TRANSFORM_TABLE,
    WINSCOPE_TRACE_RECT_TABLE,
    INPUTMETHOD_CLIENTS_TABLE,
    INPUTMETHOD_MANAGER_SERVICE_TABLE,
    INPUTMETHOD_SERVICE_TABLE,
    SURFACE_FLINGER_LAYERS_SNAPSHOT_TABLE,
    SURFACE_FLINGER_DISPLAY_TABLE,
    SURFACE_FLINGER_LAYER_TABLE,
    WINSCOPE_FILL_REGION_TABLE,
    SURFACE_FLINGER_TRANSACTIONS_TABLE,
    SURFACE_FLINGER_TRANSACTION_TABLE,
    SURFACE_FLINGER_TRANSACTION_FLAG_TABLE,
    VIEWCAPTURE_TABLE,
    VIEWCAPTURE_VIEW_TABLE,
    VIEWCAPTURE_INTERNED_DATA_TABLE,
    WINDOW_MANAGER_SHELL_TRANSITIONS_TABLE,
    WINDOW_MANAGER_SHELL_TRANSITION_HANDLERS_TABLE,
    WINDOW_MANAGER_SHELL_TRANSITION_PARTICIPANTS_TABLE,
    WINDOW_MANAGER_SHELL_TRANSITION_PROTOS_TABLE,
    WINDOW_MANAGER_TABLE,
    PROTOLOG_TABLE,
]
