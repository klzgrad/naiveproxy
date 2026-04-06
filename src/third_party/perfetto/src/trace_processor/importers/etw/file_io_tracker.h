/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ETW_FILE_IO_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ETW_FILE_IO_TRACKER_H_

#include <array>
#include <cstdint>
#include <unordered_map>

#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

// Type for an "I/O Request Packet", which identifies a file I/O operation.
using Irp = uint64_t;

// Opcodes for the file I/O event types. Source: `FileIo` class docs:
// https://learn.microsoft.com/en-us/windows/win32/etw/fileio
enum class EventType : uint32_t {
  kCreateFile = 64,
  kMinValue = kCreateFile,
  kCleanup = 65,
  kClose = 66,
  kReadFile = 67,
  kWriteFile = 68,
  kSetInformation = 69,
  kDeleteFile = 70,
  kRenameFile = 71,
  kDirectoryEnumeration = 72,
  kFlush = 73,
  kQueryFileInformation = 74,
  kFilesystemControlEvent = 75,
  kEndOperation = 76,
  kDirectoryNotification = 77,
  kMaxValue = kDirectoryNotification,
};

// Values for the "File Info" argument. Source: `FILE_INFORMATION_CLASS` docs:
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_file_information_class
enum class FileInfoClass : uint32_t {
  kFileDirectoryInformation = 1,
  kMinValue = kFileDirectoryInformation,
  kFileFullDirectoryInformation = 2,
  kFileBothDirectoryInformation = 3,
  kFileBasicInformation = 4,
  kFileStandardInformation = 5,
  kFileInternalInformation = 6,
  kFileEaInformation = 7,
  kFileAccessInformation = 8,
  kFileNameInformation = 9,
  kFileRenameInformation = 10,
  kFileLinkInformation = 11,
  kFileNamesInformation = 12,
  kFileDispositionInformation = 13,
  kFilePositionInformation = 14,
  kFileFullEaInformation = 15,
  kFileModeInformation = 16,
  kFileAlignmentInformation = 17,
  kFileAllInformation = 18,
  kFileAllocationInformation = 19,
  kFileEndOfFileInformation = 20,
  kFileAlternateNameInformation = 21,
  kFileStreamInformation = 22,
  kFilePipeInformation = 23,
  kFilePipeLocalInformation = 24,
  kFilePipeRemoteInformation = 25,
  kFileMailslotQueryInformation = 26,
  kFileMailslotSetInformation = 27,
  kFileCompressionInformation = 28,
  kFileObjectIdInformation = 29,
  kFileCompletionInformation = 30,
  kFileMoveClusterInformation = 31,
  kFileQuotaInformation = 32,
  kFileReparsePointInformation = 33,
  kFileNetworkOpenInformation = 34,
  kFileAttributeTagInformation = 35,
  kFileTrackingInformation = 36,
  kFileIdBothDirectoryInformation = 37,
  kFileIdFullDirectoryInformation = 38,
  kFileValidDataLengthInformation = 39,
  kFileShortNameInformation = 40,
  kFileIoCompletionNotificationInformation = 41,
  kFileIoStatusBlockRangeInformation = 42,
  kFileIoPriorityHintInformation = 43,
  kFileSfioReserveInformation = 44,
  kFileSfioVolumeInformation = 45,
  kFileHardLinkInformation = 46,
  kFileProcessIdsUsingFileInformation = 47,
  kFileNormalizedNameInformation = 48,
  kFileNetworkPhysicalNameInformation = 49,
  kFileIdGlobalTxDirectoryInformation = 50,
  kFileIsRemoteDeviceInformation = 51,
  kFileUnusedInformation = 52,
  kFileNumaNodeInformation = 53,
  kFileStandardLinkInformation = 54,
  kFileRemoteProtocolInformation = 55,
  kFileRenameInformationBypassAccessCheck = 56,
  kFileLinkInformationBypassAccessCheck = 57,
  kFileVolumeNameInformation = 58,
  kFileIdInformation = 59,
  kFileIdExtdDirectoryInformation = 60,
  kFileReplaceCompletionInformation = 61,
  kFileHardLinkFullIdInformation = 62,
  kFileIdExtdBothDirectoryInformation = 63,
  kFileDispositionInformationEx = 64,
  kFileRenameInformationEx = 65,
  kFileRenameInformationExBypassAccessCheck = 66,
  kFileDesiredStorageClassInformation = 67,
  kFileStatInformation = 68,
  kFileMemoryPartitionInformation = 69,
  kFileStatLxInformation = 70,
  kFileCaseSensitiveInformation = 71,
  kFileLinkInformationEx = 72,
  kFileLinkInformationExBypassAccessCheck = 73,
  kFileStorageReserveIdInformation = 74,
  kFileCaseSensitiveInformationForceAccessCheck = 75,
  kFileKnownFolderInformation = 76,
  kFileStatBasicInformation = 77,
  kFileId64ExtdDirectoryInformation = 78,
  kFileId64ExtdBothDirectoryInformation = 79,
  kFileIdAllExtdDirectoryInformation = 80,
  kFileIdAllExtdBothDirectoryInformation = 81,
  kMaxValue = kFileIdAllExtdBothDirectoryInformation,
};

// A class to keep track of file I/O events recorded by Event Tracing for
// Windows (ETW).
class FileIoTracker {
 public:
  explicit FileIoTracker(TraceProcessorContext* context);

  void ParseFileIoCreate(int64_t timestamp,
                         UniqueTid utid,
                         protozero::ConstBytes);
  void ParseFileIoDirEnum(int64_t timestamp,
                          UniqueTid utid,
                          protozero::ConstBytes);
  void ParseFileIoInfo(int64_t timestamp,
                       UniqueTid utid,
                       protozero::ConstBytes);
  void ParseFileIoReadWrite(int64_t timestamp,
                            UniqueTid utid,
                            protozero::ConstBytes);
  void ParseFileIoSimpleOp(int64_t timestamp,
                           UniqueTid utid,
                           protozero::ConstBytes);
  void ParseFileIoOpEnd(int64_t timestamp,
                        UniqueTid utid,
                        protozero::ConstBytes);

  void OnEventsFullyExtracted();

 private:
  struct StartedEvent {
    StringId name;
    int64_t timestamp;
    UniqueTid utid;
  };

  // Starts tracking `event`, to be added to the trace when its matching end
  // event is parsed.
  void StartEvent(std::optional<Irp> irp,
                  StringId name,
                  int64_t timestamp,
                  UniqueTid utid,
                  SliceTracker::SetArgsCallback args);

  // Adds the ending event to the trace as a slice.
  void EndEvent(std::optional<Irp> irp,
                int64_t timestamp,
                UniqueTid utid,
                SliceTracker::SetArgsCallback args);

  // Ends the given event with a duration of zero, and adds an argument labeling
  // it as missing a matching end event.
  void EndUnmatchedStart(Irp irp, int64_t timestamp, UniqueTid utid);

  // Records an "EndOperation" event with a duration of zero, and adds an
  // argument labeling it as missing a matching start event.
  void RecordUnmatchedEnd(int64_t timestamp,
                          UniqueTid utid,
                          SliceTracker::SetArgsCallback args);

  // Records an event without an IRP identifier with a duration of zero (as it's
  // unable to be matched with a corresponding start or end event).
  void RecordEventWithoutIrp(StringId name,
                             int64_t timestamp,
                             UniqueTid utid,
                             SliceTracker::SetArgsCallback args);

  // Helper function to get the value to display for `info_class`: either its
  // string representation, if known, or its numerical value.
  Variadic GetInfoClassValue(FileInfoClass info_class) const;

  // Helper function to get the readable name of the event with `opcode`, if
  // known.
  std::optional<StringId> GetEventName(uint32_t opcode) const;

  // Helper function to get `type`'s index in the `event_types_` array.
  static size_t GetEventTypeIndex(EventType type);

  // Helper function to get `info_class`'s index in the `file_info_classes_`
  // array.
  static size_t GetFileInfoClassIndex(FileInfoClass info_class);

  TraceProcessorContext* context_;

  // Readable descriptions for the file I/O event types.
  std::array<StringId,
             static_cast<uint32_t>(EventType::kMaxValue) -
                 static_cast<uint32_t>(EventType::kMinValue) + 1>
      event_types_{};

  // Readable descriptions for known "File Info" argument values.
  std::array<StringId,
             static_cast<uint32_t>(FileInfoClass::kMaxValue) -
                 static_cast<uint32_t>(FileInfoClass::kMinValue) + 1>
      file_info_classes_{};

  // Tracks events parsed so far for which a corresponding "operation end" event
  // has not yet been parsed. This enables events with no matching end event to
  // be closed with a zero duration at the end of parsing.
  std::unordered_map<Irp, StartedEvent> started_events_;

  // Strings interned in the constructor to improve performance.
  const StringId create_options_arg_;
  const StringId disposition_arg_;
  const StringId enumeration_path_arg_;
  const StringId extra_info_arg_;
  const StringId file_attributes_arg_;
  const StringId file_index_arg_;
  const StringId file_key_arg_;
  const StringId file_object_arg_;
  const StringId file_size_arg_;
  const StringId info_class_arg_;
  const StringId io_flags_arg_;
  const StringId irp_arg_;
  const StringId io_size_arg_;
  const StringId nt_status_arg_;
  const StringId offset_arg_;
  const StringId open_path_arg_;
  const StringId share_access_arg_;
  const StringId thread_id_arg_;
  const StringId missing_event_arg_;
  const StringId missing_start_event_;
  const StringId missing_end_event_;
  const StringId unknown_event_;
  const StringId dir_enum_event_;
  const StringId info_event_;
  const StringId read_write_event_;
  const StringId simple_op_event_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ETW_FILE_IO_TRACKER_H_
