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

#include "src/trace_processor/importers/etw/file_io_tracker.h"

#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/etw/etw.pbzero.h"
#include "protos/perfetto/trace/etw/etw_event.pbzero.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"

namespace perfetto::trace_processor {

namespace {

using protozero::ConstBytes;

// Display file I/O events in per-thread rows under the "IO > ETW File I/O"
// headers (per the schema for type "etw_fileio" in `slice_tracks.ts`).
const auto kBlueprint = TrackCompressor::SliceBlueprint(
    "etw_fileio",
    tracks::DimensionBlueprints(tracks::kThreadDimensionBlueprint));

constexpr std::pair<EventType, const char*> kEventTypeNames[] = {
    {EventType::kCreateFile, "CreateFile"},
    {EventType::kCleanup, "Cleanup"},
    {EventType::kClose, "Close"},
    {EventType::kReadFile, "ReadFile"},
    {EventType::kWriteFile, "WriteFile"},
    {EventType::kSetInformation, "SetInformation"},
    {EventType::kDeleteFile, "DeleteFile"},
    {EventType::kRenameFile, "RenameFile"},
    {EventType::kDirectoryEnumeration, "DirectoryEnumeration"},
    {EventType::kFlush, "Flush"},
    {EventType::kQueryFileInformation, "QueryFileInformation"},
    {EventType::kFilesystemControlEvent, "FilesystemControlEvent"},
    {EventType::kEndOperation, "EndOperation"},
    {EventType::kDirectoryNotification, "DirectoryNotification"}};

constexpr std::pair<FileInfoClass, const char*> kFileInfoClassNames[] = {
    {FileInfoClass::kFileDirectoryInformation, "FileDirectoryInformation"},
    {FileInfoClass::kFileFullDirectoryInformation,
     "FileFullDirectoryInformation"},
    {FileInfoClass::kFileBothDirectoryInformation,
     "FileBothDirectoryInformation"},
    {FileInfoClass::kFileBasicInformation, "FileBasicInformation"},
    {FileInfoClass::kFileStandardInformation, "FileStandardInformation"},
    {FileInfoClass::kFileInternalInformation, "FileInternalInformation"},
    {FileInfoClass::kFileEaInformation, "FileEaInformation"},
    {FileInfoClass::kFileAccessInformation, "FileAccessInformation"},
    {FileInfoClass::kFileNameInformation, "FileNameInformation"},
    {FileInfoClass::kFileRenameInformation, "FileRenameInformation"},
    {FileInfoClass::kFileLinkInformation, "FileLinkInformation"},
    {FileInfoClass::kFileNamesInformation, "FileNamesInformation"},
    {FileInfoClass::kFileDispositionInformation, "FileDispositionInformation"},
    {FileInfoClass::kFilePositionInformation, "FilePositionInformation"},
    {FileInfoClass::kFileFullEaInformation, "FileFullEaInformation"},
    {FileInfoClass::kFileModeInformation, "FileModeInformation"},
    {FileInfoClass::kFileAlignmentInformation, "FileAlignmentInformation"},
    {FileInfoClass::kFileAllInformation, "FileAllInformation"},
    {FileInfoClass::kFileAllocationInformation, "FileAllocationInformation"},
    {FileInfoClass::kFileEndOfFileInformation, "FileEndOfFileInformation"},
    {FileInfoClass::kFileAlternateNameInformation,
     "FileAlternateNameInformation"},
    {FileInfoClass::kFileStreamInformation, "FileStreamInformation"},
    {FileInfoClass::kFilePipeInformation, "FilePipeInformation"},
    {FileInfoClass::kFilePipeLocalInformation, "FilePipeLocalInformation"},
    {FileInfoClass::kFilePipeRemoteInformation, "FilePipeRemoteInformation"},
    {FileInfoClass::kFileMailslotQueryInformation,
     "FileMailslotQueryInformation"},
    {FileInfoClass::kFileMailslotSetInformation, "FileMailslotSetInformation"},
    {FileInfoClass::kFileCompressionInformation, "FileCompressionInformation"},
    {FileInfoClass::kFileObjectIdInformation, "FileObjectIdInformation"},
    {FileInfoClass::kFileCompletionInformation, "FileCompletionInformation"},
    {FileInfoClass::kFileMoveClusterInformation, "FileMoveClusterInformation"},
    {FileInfoClass::kFileQuotaInformation, "FileQuotaInformation"},
    {FileInfoClass::kFileReparsePointInformation,
     "FileReparsePointInformation"},
    {FileInfoClass::kFileNetworkOpenInformation, "FileNetworkOpenInformation"},
    {FileInfoClass::kFileAttributeTagInformation,
     "FileAttributeTagInformation"},
    {FileInfoClass::kFileTrackingInformation, "FileTrackingInformation"},
    {FileInfoClass::kFileIdBothDirectoryInformation,
     "FileIdBothDirectoryInformation"},
    {FileInfoClass::kFileIdFullDirectoryInformation,
     "FileIdFullDirectoryInformation"},
    {FileInfoClass::kFileValidDataLengthInformation,
     "FileValidDataLengthInformation"},
    {FileInfoClass::kFileShortNameInformation, "FileShortNameInformation"},
    {FileInfoClass::kFileIoCompletionNotificationInformation,
     "FileIoCompletionNotificationInformation"},
    {FileInfoClass::kFileIoStatusBlockRangeInformation,
     "FileIoStatusBlockRangeInformation"},
    {FileInfoClass::kFileIoPriorityHintInformation,
     "FileIoPriorityHintInformation"},
    {FileInfoClass::kFileSfioReserveInformation, "FileSfioReserveInformation"},
    {FileInfoClass::kFileSfioVolumeInformation, "FileSfioVolumeInformation"},
    {FileInfoClass::kFileHardLinkInformation, "FileHardLinkInformation"},
    {FileInfoClass::kFileProcessIdsUsingFileInformation,
     "FileProcessIdsUsingFileInformation"},
    {FileInfoClass::kFileNormalizedNameInformation,
     "FileNormalizedNameInformation"},
    {FileInfoClass::kFileNetworkPhysicalNameInformation,
     "FileNetworkPhysicalNameInformation"},
    {FileInfoClass::kFileIdGlobalTxDirectoryInformation,
     "FileIdGlobalTxDirectoryInformation"},
    {FileInfoClass::kFileIsRemoteDeviceInformation,
     "FileIsRemoteDeviceInformation"},
    {FileInfoClass::kFileUnusedInformation, "FileUnusedInformation"},
    {FileInfoClass::kFileNumaNodeInformation, "FileNumaNodeInformation"},
    {FileInfoClass::kFileStandardLinkInformation,
     "FileStandardLinkInformation"},
    {FileInfoClass::kFileRemoteProtocolInformation,
     "FileRemoteProtocolInformation"},
    {FileInfoClass::kFileRenameInformationBypassAccessCheck,
     "FileRenameInformationBypassAccessCheck"},
    {FileInfoClass::kFileLinkInformationBypassAccessCheck,
     "FileLinkInformationBypassAccessCheck"},
    {FileInfoClass::kFileVolumeNameInformation, "FileVolumeNameInformation"},
    {FileInfoClass::kFileIdInformation, "FileIdInformation"},
    {FileInfoClass::kFileIdExtdDirectoryInformation,
     "FileIdExtdDirectoryInformation"},
    {FileInfoClass::kFileReplaceCompletionInformation,
     "FileReplaceCompletionInformation"},
    {FileInfoClass::kFileHardLinkFullIdInformation,
     "FileHardLinkFullIdInformation"},
    {FileInfoClass::kFileIdExtdBothDirectoryInformation,
     "FileIdExtdBothDirectoryInformation"},
    {FileInfoClass::kFileDispositionInformationEx,
     "FileDispositionInformationEx"},
    {FileInfoClass::kFileRenameInformationEx, "FileRenameInformationEx"},
    {FileInfoClass::kFileRenameInformationExBypassAccessCheck,
     "FileRenameInformationExBypassAccessCheck"},
    {FileInfoClass::kFileDesiredStorageClassInformation,
     "FileDesiredStorageClassInformation"},
    {FileInfoClass::kFileStatInformation, "FileStatInformation"},
    {FileInfoClass::kFileMemoryPartitionInformation,
     "FileMemoryPartitionInformation"},
    {FileInfoClass::kFileStatLxInformation, "FileStatLxInformation"},
    {FileInfoClass::kFileCaseSensitiveInformation,
     "FileCaseSensitiveInformation"},
    {FileInfoClass::kFileLinkInformationEx, "FileLinkInformationEx"},
    {FileInfoClass::kFileLinkInformationExBypassAccessCheck,
     "FileLinkInformationExBypassAccessCheck"},
    {FileInfoClass::kFileStorageReserveIdInformation,
     "FileStorageReserveIdInformation"},
    {FileInfoClass::kFileCaseSensitiveInformationForceAccessCheck,
     "FileCaseSensitiveInformationForceAccessCheck"},
    {FileInfoClass::kFileKnownFolderInformation, "FileKnownFolderInformation"},
    {FileInfoClass::kFileStatBasicInformation, "FileStatBasicInformation"},
    {FileInfoClass::kFileId64ExtdDirectoryInformation,
     "FileId64ExtdDirectoryInformation"},
    {FileInfoClass::kFileId64ExtdBothDirectoryInformation,
     "FileId64ExtdBothDirectoryInformation"},
    {FileInfoClass::kFileIdAllExtdDirectoryInformation,
     "FileIdAllExtdDirectoryInformation"},
    {FileInfoClass::kFileIdAllExtdBothDirectoryInformation,
     "FileIdAllExtdBothDirectoryInformation"}};

}  // namespace

FileIoTracker::FileIoTracker(TraceProcessorContext* context)
    : context_(context),
      // Argument field names:
      create_options_arg_(context->storage->InternString("Create Options")),
      disposition_arg_(context->storage->InternString("Disposition")),
      enumeration_path_arg_(context->storage->InternString("Enumeration Path")),
      extra_info_arg_(context->storage->InternString("Extra Info")),
      file_attributes_arg_(context->storage->InternString("File Attributes")),
      file_index_arg_(context->storage->InternString("File Index")),
      file_key_arg_(context->storage->InternString("File Key")),
      file_object_arg_(context->storage->InternString("File Object")),
      file_size_arg_(context->storage->InternString("File Size")),
      info_class_arg_(context->storage->InternString("Info Class")),
      io_flags_arg_(context->storage->InternString("I/O Flags")),
      irp_arg_(context->storage->InternString("I/O Request Packet")),
      io_size_arg_(context->storage->InternString("I/O Size")),
      nt_status_arg_(context->storage->InternString("NT Status")),
      offset_arg_(context->storage->InternString("Offset")),
      open_path_arg_(context->storage->InternString("Open Path")),
      share_access_arg_(context->storage->InternString("Share Access")),
      thread_id_arg_(context->storage->InternString("Thread ID")),
      // Labels for events with a missing start or end:
      missing_event_arg_(context->storage->InternString("Missing Event")),
      missing_start_event_(context->storage->InternString("Start")),
      missing_end_event_(context->storage->InternString("End")),
      // Generic event names for when the event opcode is unknown:
      unknown_event_(context->storage->InternString("Unknown")),
      dir_enum_event_(context->storage->InternString("DirEnum")),
      info_event_(context->storage->InternString("Info")),
      read_write_event_(context->storage->InternString("ReadOrWrite")),
      simple_op_event_(context->storage->InternString("SimpleOp")) {
  for (const auto& event_type : kEventTypeNames) {
    event_types_[GetEventTypeIndex(event_type.first)] =
        context->storage->InternString(event_type.second);
  }
  for (const auto& info_class : kFileInfoClassNames) {
    file_info_classes_[GetFileInfoClassIndex(info_class.first)] =
        context->storage->InternString(info_class.second);
  }
}

void FileIoTracker::ParseFileIoCreate(int64_t timestamp,
                                      UniqueTid utid,
                                      ConstBytes blob) {
  protos::pbzero::FileIoCreateEtwEvent::Decoder decoder(blob);
  SliceTracker::SetArgsCallback args =
      [this, &decoder](ArgsTracker::BoundInserter* inserter) {
        if (decoder.has_irp_ptr()) {
          inserter->AddArg(irp_arg_, Variadic::Pointer(decoder.irp_ptr()));
        }
        if (decoder.has_file_object()) {
          inserter->AddArg(file_object_arg_,
                           Variadic::Pointer(decoder.file_object()));
        }
        if (decoder.has_ttid()) {
          inserter->AddArg(thread_id_arg_,
                           Variadic::UnsignedInteger(decoder.ttid()));
        }
        if (decoder.has_create_options()) {
          inserter->AddArg(create_options_arg_,
                           Variadic::Pointer(decoder.create_options()));
        }
        if (decoder.has_file_attributes()) {
          inserter->AddArg(file_attributes_arg_,
                           Variadic::Pointer(decoder.file_attributes()));
        }
        if (decoder.has_share_access()) {
          inserter->AddArg(share_access_arg_,
                           Variadic::Pointer(decoder.share_access()));
        }
        if (decoder.has_open_path()) {
          inserter->AddArg(open_path_arg_,
                           Variadic::String(context_->storage->InternString(
                               decoder.open_path())));
        }
      };
  StartEvent(
      decoder.has_irp_ptr() ? std::optional(decoder.irp_ptr()) : std::nullopt,
      event_types_.at(GetEventTypeIndex(EventType::kCreateFile)), timestamp,
      utid, std::move(args));
}

void FileIoTracker::ParseFileIoDirEnum(int64_t timestamp,
                                       UniqueTid utid,
                                       ConstBytes blob) {
  protos::pbzero::FileIoDirEnumEtwEvent::Decoder decoder(blob);
  SliceTracker::SetArgsCallback args =
      [this, &decoder](ArgsTracker::BoundInserter* inserter) {
        if (decoder.has_irp_ptr()) {
          inserter->AddArg(irp_arg_, Variadic::Pointer(decoder.irp_ptr()));
        }
        if (decoder.has_file_object()) {
          inserter->AddArg(file_object_arg_,
                           Variadic::Pointer(decoder.file_object()));
        }
        if (decoder.has_file_key()) {
          inserter->AddArg(file_key_arg_,
                           Variadic::Pointer(decoder.file_key()));
        }
        if (decoder.has_ttid()) {
          inserter->AddArg(thread_id_arg_,
                           Variadic::UnsignedInteger(decoder.ttid()));
        }
        if (decoder.has_info_class()) {
          inserter->AddArg(info_class_arg_,
                           GetInfoClassValue(static_cast<FileInfoClass>(
                               decoder.info_class())));
        }
        if (decoder.has_file_index()) {
          inserter->AddArg(file_index_arg_,
                           Variadic::UnsignedInteger(decoder.file_index()));
        }
        if (decoder.has_file_name()) {
          inserter->AddArg(enumeration_path_arg_,
                           Variadic::String(context_->storage->InternString(
                               decoder.file_name())));
        }
      };
  // Get event name from the opcode if possible, otherwise use a generic name.
  const StringId name =
      decoder.has_opcode()
          ? GetEventName(decoder.opcode()).value_or(dir_enum_event_)
          : dir_enum_event_;
  StartEvent(
      decoder.has_irp_ptr() ? std::optional(decoder.irp_ptr()) : std::nullopt,
      name, timestamp, utid, std::move(args));
}

void FileIoTracker::ParseFileIoInfo(int64_t timestamp,
                                    UniqueTid utid,
                                    ConstBytes blob) {
  protos::pbzero::FileIoInfoEtwEvent::Decoder decoder(blob);
  SliceTracker::SetArgsCallback args =
      [this, &decoder](ArgsTracker::BoundInserter* inserter) {
        if (decoder.has_irp_ptr()) {
          inserter->AddArg(irp_arg_, Variadic::Pointer(decoder.irp_ptr()));
        }
        if (decoder.has_file_object()) {
          inserter->AddArg(file_object_arg_,
                           Variadic::Pointer(decoder.file_object()));
        }
        if (decoder.has_file_key()) {
          inserter->AddArg(file_key_arg_,
                           Variadic::Pointer(decoder.file_key()));
        }
        if (decoder.has_extra_info()) {
          auto extra_info_arg = extra_info_arg_;
          if (decoder.has_info_class()) {
            // Replace "Extra Info" with a more specific descriptor when the
            // type of information is known, per
            // https://learn.microsoft.com/en-us/windows/win32/etw/fileio-info.
            const auto info_class =
                static_cast<FileInfoClass>(decoder.info_class());
            if (info_class == FileInfoClass::kFileDispositionInformation) {
              extra_info_arg = disposition_arg_;
            } else if (info_class == FileInfoClass::kFileEndOfFileInformation ||
                       info_class ==
                           FileInfoClass::kFileAllocationInformation) {
              extra_info_arg = file_size_arg_;
            }
          }
          inserter->AddArg(extra_info_arg,
                           Variadic::UnsignedInteger(decoder.extra_info()));
        }
        if (decoder.has_ttid()) {
          inserter->AddArg(thread_id_arg_,
                           Variadic::UnsignedInteger(decoder.ttid()));
        }
        if (decoder.has_info_class()) {
          inserter->AddArg(info_class_arg_,
                           GetInfoClassValue(static_cast<FileInfoClass>(
                               decoder.info_class())));
        }
      };
  // Get event name from the opcode if possible, otherwise use a generic name.
  const StringId name =
      decoder.has_opcode()
          ? GetEventName(decoder.opcode()).value_or(info_event_)
          : info_event_;
  StartEvent(
      decoder.has_irp_ptr() ? std::optional(decoder.irp_ptr()) : std::nullopt,
      name, timestamp, utid, std::move(args));
}

void FileIoTracker::ParseFileIoReadWrite(int64_t timestamp,
                                         UniqueTid utid,
                                         ConstBytes blob) {
  protos::pbzero::FileIoReadWriteEtwEvent::Decoder decoder(blob);
  SliceTracker::SetArgsCallback args =
      [this, &decoder](ArgsTracker::BoundInserter* inserter) {
        if (decoder.has_irp_ptr()) {
          inserter->AddArg(irp_arg_, Variadic::Pointer(decoder.irp_ptr()));
        }
        if (decoder.has_offset()) {
          inserter->AddArg(offset_arg_,
                           Variadic::UnsignedInteger(decoder.offset()));
        }
        if (decoder.has_file_object()) {
          inserter->AddArg(file_object_arg_,
                           Variadic::Pointer(decoder.file_object()));
        }
        if (decoder.has_file_key()) {
          inserter->AddArg(file_key_arg_,
                           Variadic::Pointer(decoder.file_key()));
        }
        if (decoder.has_ttid()) {
          inserter->AddArg(thread_id_arg_,
                           Variadic::UnsignedInteger(decoder.ttid()));
        }
        if (decoder.has_io_size()) {
          inserter->AddArg(io_size_arg_,
                           Variadic::UnsignedInteger(decoder.io_size()));
        }
        if (decoder.has_io_flags()) {
          inserter->AddArg(io_flags_arg_,
                           Variadic::Pointer(decoder.io_flags()));
        }
      };
  // Get event name from the opcode if possible, otherwise use a generic name.
  const StringId name =
      decoder.has_opcode()
          ? GetEventName(decoder.opcode()).value_or(read_write_event_)
          : read_write_event_;
  StartEvent(
      decoder.has_irp_ptr() ? std::optional(decoder.irp_ptr()) : std::nullopt,
      name, timestamp, utid, std::move(args));
}

void FileIoTracker::ParseFileIoSimpleOp(int64_t timestamp,
                                        UniqueTid utid,
                                        ConstBytes blob) {
  protos::pbzero::FileIoSimpleOpEtwEvent::Decoder decoder(blob);
  SliceTracker::SetArgsCallback args =
      [this, &decoder](ArgsTracker::BoundInserter* inserter) {
        if (decoder.has_irp_ptr()) {
          inserter->AddArg(irp_arg_, Variadic::Pointer(decoder.irp_ptr()));
        }
        if (decoder.has_file_object()) {
          inserter->AddArg(file_object_arg_,
                           Variadic::Pointer(decoder.file_object()));
        }
        if (decoder.has_file_key()) {
          inserter->AddArg(file_key_arg_,
                           Variadic::Pointer(decoder.file_key()));
        }
        if (decoder.has_ttid()) {
          inserter->AddArg(thread_id_arg_,
                           Variadic::UnsignedInteger(decoder.ttid()));
        }
      };
  // Get event name from the opcode if possible, otherwise use a generic name.
  const StringId name =
      decoder.has_opcode()
          ? GetEventName(decoder.opcode()).value_or(simple_op_event_)
          : simple_op_event_;
  StartEvent(
      decoder.has_irp_ptr() ? std::optional(decoder.irp_ptr()) : std::nullopt,
      name, timestamp, utid, std::move(args));
}

void FileIoTracker::ParseFileIoOpEnd(int64_t timestamp,
                                     UniqueTid utid,
                                     ConstBytes blob) {
  protos::pbzero::FileIoOpEndEtwEvent::Decoder decoder(blob);
  SliceTracker::SetArgsCallback args =
      [this, &decoder](ArgsTracker::BoundInserter* inserter) {
        if (decoder.has_extra_info()) {
          inserter->AddArg(extra_info_arg_,
                           Variadic::UnsignedInteger(decoder.extra_info()));
        }
        if (decoder.has_nt_status()) {
          inserter->AddArg(nt_status_arg_,
                           Variadic::Pointer(decoder.nt_status()));
        }
      };
  EndEvent(
      decoder.has_irp_ptr() ? std::optional(decoder.irp_ptr()) : std::nullopt,
      timestamp, utid, std::move(args));
}

void FileIoTracker::OnEventsFullyExtracted() {
  while (!started_events_.empty()) {
    // `EndUnmatchedStart()` removes the recorded event, so retrieve the first
    // event each loop.
    const auto& [irp, started_event] = *started_events_.begin();
    EndUnmatchedStart(irp, started_event.timestamp, started_event.utid);
  }
}

void FileIoTracker::StartEvent(std::optional<Irp> irp,
                               StringId name,
                               int64_t timestamp,
                               UniqueTid utid,
                               SliceTracker::SetArgsCallback args) {
  if (!irp.has_value()) {
    RecordEventWithoutIrp(name, timestamp, utid, std::move(args));
    return;
  }

  const auto previous_event_same_irp = started_events_.find(*irp);
  if (previous_event_same_irp != started_events_.end()) {
    // The last event using this IRP never ended. Since the IRP is being reused,
    // the previous event must be done and its end event must have been dropped.
    EndUnmatchedStart(*irp, previous_event_same_irp->second.timestamp,
                      previous_event_same_irp->second.utid);
  }

  // `track_id` controls the row the events appear in. This must be created via
  // `TrackCompressor` because slices may be partially overlapping, which is not
  // supported by the Perfetto data model as-is.
  const auto track_id = context_->track_compressor->InternBegin(
      kBlueprint, tracks::Dimensions(utid),
      /*cookie=*/static_cast<int64_t>(*irp));

  // Begin a slice for the event.
  context_->slice_tracker->Begin(timestamp, track_id, kNullStringId, name,
                                 std::move(args));
  started_events_[*irp] = {name, timestamp, utid};
}

void FileIoTracker::EndEvent(std::optional<Irp> irp,
                             int64_t timestamp,
                             UniqueTid utid,
                             SliceTracker::SetArgsCallback args) {
  if (!irp.has_value()) {
    RecordEventWithoutIrp(
        event_types_.at(GetEventTypeIndex(EventType::kEndOperation)), timestamp,
        utid, std::move(args));
    return;
  }

  // Get the matching start event.
  const auto started_event = started_events_.find(*irp);
  if (started_event == started_events_.end()) {
    // This end event has no corresponding start.
    RecordUnmatchedEnd(timestamp, utid, std::move(args));
    return;
  }
  const auto name = started_event->second.name;

  // End the slice for this event.
  const auto track_id = context_->track_compressor->InternEnd(
      kBlueprint, tracks::Dimensions(utid),
      /*cookie=*/static_cast<int64_t>(*irp));
  context_->slice_tracker->End(timestamp, track_id, kNullStringId, name,
                               std::move(args));
  started_events_.erase(started_event);
}

void FileIoTracker::EndUnmatchedStart(Irp irp,
                                      int64_t timestamp,
                                      UniqueTid utid) {
  // End the given event with a duration of zero.
  EndEvent(irp, timestamp, utid, [this](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(missing_event_arg_, Variadic::String(missing_end_event_));
  });
}

void FileIoTracker::RecordUnmatchedEnd(int64_t timestamp,
                                       UniqueTid utid,
                                       SliceTracker::SetArgsCallback args) {
  // Add a single "EndOperation" event with a duration of zero.
  const int64_t duration = 0;
  const auto track_id = context_->track_compressor->InternScoped(
      kBlueprint, tracks::Dimensions(utid), timestamp, duration);
  context_->slice_tracker->Scoped(
      timestamp, track_id, kNullStringId,
      event_types_.at(GetEventTypeIndex(EventType::kEndOperation)), duration,
      [this, args = std::move(args)](ArgsTracker::BoundInserter* inserter) {
        if (args) {
          args(inserter);
        }
        inserter->AddArg(missing_event_arg_,
                         Variadic::String(missing_start_event_));
      });
}

void FileIoTracker::RecordEventWithoutIrp(StringId name,
                                          int64_t timestamp,
                                          UniqueTid utid,
                                          SliceTracker::SetArgsCallback args) {
  const int64_t duration = 0;
  const auto track_id = context_->track_compressor->InternScoped(
      kBlueprint, tracks::Dimensions(utid), timestamp, duration);
  context_->slice_tracker->Scoped(timestamp, track_id, kNullStringId, name,
                                  duration, std::move(args));
}

Variadic FileIoTracker::GetInfoClassValue(FileInfoClass info_class) const {
  auto info_class_val = static_cast<uint32_t>(info_class);
  if (info_class_val < static_cast<uint32_t>(FileInfoClass::kMinValue) ||
      info_class_val > static_cast<uint32_t>(FileInfoClass::kMaxValue)) {
    return Variadic::UnsignedInteger(info_class_val);
  }
  auto string_id = file_info_classes_[GetFileInfoClassIndex(info_class)];
  if (string_id.is_null()) {
    return Variadic::UnsignedInteger(info_class_val);
  }
  return Variadic::String(string_id);
}

std::optional<StringId> FileIoTracker::GetEventName(uint32_t opcode) const {
  if (opcode < static_cast<uint32_t>(EventType::kMinValue) ||
      opcode > static_cast<uint32_t>(EventType::kMaxValue)) {
    return std::nullopt;
  }
  return event_types_[GetEventTypeIndex(static_cast<EventType>(opcode))];
}

// static
size_t FileIoTracker::GetEventTypeIndex(EventType type) {
  return static_cast<size_t>(static_cast<uint32_t>(type) -
                             static_cast<uint32_t>(EventType::kMinValue));
}

// static
size_t FileIoTracker::GetFileInfoClassIndex(FileInfoClass info_class) {
  return static_cast<size_t>(static_cast<uint32_t>(info_class) -
                             static_cast<uint32_t>(FileInfoClass::kMinValue));
}

}  // namespace perfetto::trace_processor
