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
from python.generators.trace_processor_table.public import CppAccess
from python.generators.trace_processor_table.public import CppAccessDuration
from python.generators.trace_processor_table.public import CppInt32
from python.generators.trace_processor_table.public import CppInt64
from python.generators.trace_processor_table.public import CppOptional
from python.generators.trace_processor_table.public import CppString
from python.generators.trace_processor_table.public import Table
from python.generators.trace_processor_table.public import TableDoc

ANDROID_VIDEO_FRAMES_TABLE = Table(
    python_module=__file__,
    class_name='AndroidVideoFramesTable',
    sql_name='__intrinsic_video_frames',
    columns=[
        C('ts',
          CppInt64(),
          cpp_access=CppAccess.READ,
          cpp_access_duration=CppAccessDuration.POST_FINALIZATION),
        C('display_id', CppInt32()),
        C('display_name', CppOptional(CppString())),
        C('codec_string', CppOptional(CppString())),
        C('frame_number', CppInt64()),
        C('codec', CppOptional(CppInt32())),
        C('is_key_frame', CppOptional(CppInt32())),
        C('pts_us', CppOptional(CppInt64())),
        C('is_config', CppOptional(CppInt32())),
    ],
    tabledoc=TableDoc(
        doc='''
          Video frames captured from device displays. The encoded payload is
          held zero-copy; fetch the bytes with
          __INTRINSIC_VIDEO_FRAME_AU_DATA(id), which returns a BLOB.
        ''',
        group='Android',
        columns={
            'ts': 'Timestamp of the frame capture.',
            'display_id': 'Identifies the source display. The UI groups '
                          'frames into per-display tracks by this value.',
            'display_name': 'Human-readable display name; set on '
                            'codec_config rows and propagated to all rows of '
                            'the same display_id.',
            'codec_string': 'RFC 6381 codec string (e.g. "avc1.42c00b"); '
                            'set on codec_config rows and propagated to all '
                            'rows of the same display_id.',
            'frame_number': 'Sequential frame number within the session.',
            'codec': 'VideoFrame.Codec (1=H264, 2=HEVC).',
            'is_key_frame': 'For access units: 1 if a key frame (IDR).',
            'pts_us': 'For access units: codec presentation timestamp (us).',
            'is_config': '1 if this row carries codec_config '
                         '(decoder setup), not a displayable frame.',
        }))

# Keep this list sorted.
ALL_TABLES = [
    ANDROID_VIDEO_FRAMES_TABLE,
]
