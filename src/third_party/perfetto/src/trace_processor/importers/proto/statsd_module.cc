/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/statsd_module.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "protos/perfetto/trace/statsd/statsd_atom.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/importers/proto/atoms.descriptor.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

namespace perfetto::trace_processor {
namespace {

constexpr const char* kAtomProtoName = ".android.os.statsd.Atom";

// If we don't know about the atom format put whatever details we
// can. This has the following restrictions:
// - We can't tell the difference between double, fixed64, sfixed64
//   so those all show up as double
// - We can't tell the difference between float, fixed32, sfixed32
//   so those all show up as float
// - We can't tell the difference between int32, int64 and sint32
//   and sint64. We assume int32/int64.
// - We only show the length of strings, nested messages, packed ints
//   and any other length delimited fields.
base::Status ParseGenericEvent(const protozero::ConstBytes& cb,
                               util::ProtoToArgsParser::Delegate& delegate) {
  protozero::ProtoDecoder decoder(cb);
  for (auto f = decoder.ReadField(); f.valid(); f = decoder.ReadField()) {
    switch (f.type()) {
      case protozero::proto_utils::ProtoWireType::kLengthDelimited: {
        base::StackString<64> name("field_%u", f.id());
        std::string name_str = name.ToStdString();
        util::ProtoToArgsParser::Key key{name_str, name_str};
        delegate.AddBytes(key, f.as_bytes());
        break;
      }
      case protozero::proto_utils::ProtoWireType::kVarInt: {
        base::StackString<64> name("field_%u", f.id());
        std::string name_str = name.ToStdString();
        util::ProtoToArgsParser::Key key{name_str, name_str};
        delegate.AddInteger(key, f.as_int64());
        break;
      }
      case protozero::proto_utils::ProtoWireType::kFixed32: {
        base::StackString<64> name("field_%u_assuming_float", f.id());
        std::string name_str = name.ToStdString();
        util::ProtoToArgsParser::Key key{name_str, name_str};
        delegate.AddDouble(key, static_cast<double>(f.as_float()));
        break;
      }
      case protozero::proto_utils::ProtoWireType::kFixed64: {
        base::StackString<64> name("field_%u_assuming_double", f.id());
        std::string name_str = name.ToStdString();
        util::ProtoToArgsParser::Key key{name_str, name_str};
        delegate.AddDouble(key, f.as_double());
        break;
      }
    }
  }
  return base::OkStatus();
}

}  // namespace

using perfetto::protos::pbzero::StatsdAtom;
using perfetto::protos::pbzero::TracePacket;

StatsdModule::StatsdModule(ProtoImporterModuleContext* module_context,
                           TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_(context),
      args_parser_(*context_->descriptor_pool_) {
  RegisterForField(TracePacket::kStatsdAtomFieldNumber);
  context_->descriptor_pool_->AddFromFileDescriptorSet(
      kAtomsDescriptor.data(), kAtomsDescriptor.size(), {},
      true);  // To allow merging of extra descriptors from statsd
  if (auto i = context_->descriptor_pool_->FindDescriptorIdx(kAtomProtoName)) {
    descriptor_idx_ = *i;
  } else {
    PERFETTO_FATAL("Failed to find descriptor for %s", kAtomProtoName);
  }
}

StatsdModule::~StatsdModule() = default;

ModuleResult StatsdModule::TokenizePacket(
    const TracePacket::Decoder& decoder,
    TraceBlobView* /*packet*/,
    int64_t packet_timestamp,
    RefPtr<PacketSequenceStateGeneration> state,
    uint32_t field_id) {
  if (field_id != TracePacket::kStatsdAtomFieldNumber) {
    return ModuleResult::Ignored();
  }
  const auto& atoms_wrapper = StatsdAtom::Decoder(decoder.statsd_atom());
  auto it_timestamps = atoms_wrapper.timestamp_nanos();
  for (auto it = atoms_wrapper.atom(); it; ++it) {
    int64_t atom_timestamp;

    if (it_timestamps) {
      atom_timestamp = *it_timestamps++;
    } else {
      context_->storage->IncrementStats(stats::atom_timestamp_missing);
      atom_timestamp = packet_timestamp;
    }

    protozero::HeapBuffered<TracePacket> forged;
    forged->set_timestamp(static_cast<uint64_t>(atom_timestamp));

    auto* statsd = forged->set_statsd_atom();
    statsd->AppendBytes(StatsdAtom::kAtomFieldNumber, (*it).data, (*it).size);

    auto [vec, size] = forged.SerializeAsUniquePtr();
    TraceBlobView tbv(TraceBlob::TakeOwnership(std::move(vec), size));
    module_context_->trace_packet_stream->Push(
        atom_timestamp, TracePacketData{std::move(tbv), state});
  }

  return ModuleResult::Handled();
}

void StatsdModule::ParseTracePacketData(const TracePacket::Decoder& decoder,
                                        int64_t ts,
                                        const TracePacketData&,
                                        uint32_t field_id) {
  if (field_id != TracePacket::kStatsdAtomFieldNumber) {
    return;
  }
  const auto& atoms_wrapper = StatsdAtom::Decoder(decoder.statsd_atom());
  auto it = atoms_wrapper.atom();
  // There should be exactly one atom per trace packet at this point.
  // If not something has gone wrong in tokenization above.
  PERFETTO_CHECK(it);
  ParseAtom(ts, *it++);
  PERFETTO_CHECK(!it);
}

void StatsdModule::ParseAtom(int64_t ts, protozero::ConstBytes nested_bytes) {
  // nested_bytes is an Atom proto. We (deliberately) don't generate
  // decoding code for every kind of atom (or the parent Atom proto)
  // and instead use the descriptor to parse the args/name.

  // Atom is a giant oneof of all the possible 'kinds' of atom so here
  // we use the protozero decoder implementation to grab the first
  // field id which we we use to look up the field name:
  protozero::ProtoDecoder nested_decoder(nested_bytes);
  protozero::Field field = nested_decoder.ReadField();
  uint32_t nested_field_id = 0;
  if (field.valid()) {
    nested_field_id = field.id();
  }
  StringId atom_name = GetAtomName(nested_field_id);
  context_->slice_tracker->Scoped(
      ts, InternTrackId(), kNullStringId, atom_name, 0,
      [&](ArgsTracker::BoundInserter* inserter) {
        ArgsParser delegate(ts, *inserter, *context_->storage);

        const auto& descriptor =
            context_->descriptor_pool_->descriptors()[descriptor_idx_];
        const auto& field_it = descriptor.fields().find(nested_field_id);
        base::Status status;

        if (field_it == descriptor.fields().end()) {
          /// Field ids 100000 and over are OEM atoms - we can't have the
          // descriptor for them so don't report errors. See:
          // https://cs.android.com/android/platform/superproject/main/+/main:frameworks/proto_logging/stats/atoms.proto;l=1290;drc=a34b11bfebe897259a0340a59f1793ae2dffd762
          if (nested_field_id < 100000) {
            context_->storage->IncrementStats(stats::atom_unknown);
          }

          status = ParseGenericEvent(field.as_bytes(), delegate);
        } else {
          status = args_parser_.ParseMessage(nested_bytes, kAtomProtoName,
                                             nullptr /* parse all fields */,
                                             delegate);
        }

        if (!status.ok()) {
          context_->storage->IncrementStats(stats::atom_unknown);
        }
      });
}

StringId StatsdModule::GetAtomName(uint32_t atom_field_id) {
  StringId* cached_name = atom_names_.Find(atom_field_id);
  if (cached_name == nullptr) {
    if (!descriptor_idx_) {
      context_->storage->IncrementStats(stats::atom_unknown);
      return context_->storage->InternString("Could not load atom descriptor");
    }

    const auto& descriptor =
        context_->descriptor_pool_->descriptors()[descriptor_idx_];
    StringId name_id;
    const auto& field_it = descriptor.fields().find(atom_field_id);
    if (field_it == descriptor.fields().end()) {
      base::StackString<255> name("atom_%u", atom_field_id);
      name_id = context_->storage->InternString(name.string_view());
    } else {
      const FieldDescriptor& field = field_it->second;
      name_id = context_->storage->InternString(base::StringView(field.name()));
    }
    atom_names_[atom_field_id] = name_id;
    return name_id;
  }
  return *cached_name;
}

TrackId StatsdModule::InternTrackId() {
  if (!track_id_) {
    static constexpr auto kBlueprint =
        tracks::SliceBlueprint("statsd_atoms", tracks::DimensionBlueprints(),
                               tracks::StaticNameBlueprint("Statsd Atoms"));
    track_id_ = context_->track_tracker->InternTrack(kBlueprint);
  }
  return *track_id_;
}

}  // namespace perfetto::trace_processor
