/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/network_trace_module.h"

#include <cinttypes>
#include <cstdint>
#include <optional>
#include <utility>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "protos/perfetto/trace/android/network_trace.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/types/tcp_state.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace {

// From android.os.UserHandle.PER_USER_RANGE
constexpr int kPerUserRange = 100000;

// Convert the bitmask into a string where '.' indicates an unset bit
// and each bit gets a unique letter if set. The letters correspond to
// the bitfields in tcphdr (fin, syn, rst, etc).
base::StackString<12> GetTcpFlagMask(uint32_t tcp_flags) {
  static constexpr char kBitNames[] = "fsrpauec";
  static constexpr int kBitCount = 8;

  char flags[kBitCount + 1] = {'\0'};
  for (int f = 0; f < kBitCount; f++) {
    flags[f] = (tcp_flags & (1 << f)) ? kBitNames[f] : '.';
  }

  return base::StackString<12>("%s", flags);
}

}  // namespace

using ::perfetto::protos::pbzero::NetworkPacketBundle;
using ::perfetto::protos::pbzero::NetworkPacketEvent;
using ::perfetto::protos::pbzero::TracePacket;
using ::perfetto::protos::pbzero::TrafficDirection;
using ::protozero::ConstBytes;

NetworkTraceModule::NetworkTraceModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_(context),
      net_arg_length_(context->storage->InternString("packet_length")),
      net_arg_ip_proto_(context->storage->InternString("packet_transport")),
      net_arg_tcp_flags_(context->storage->InternString("packet_tcp_flags")),
      net_arg_tag_(context->storage->InternString("socket_tag")),
      net_arg_uid_(context->storage->InternString("socket_uid")),
      net_arg_local_port_(context->storage->InternString("local_port")),
      net_arg_remote_port_(context->storage->InternString("remote_port")),
      net_arg_icmp_type_(context->storage->InternString("packet_icmp_type")),
      net_arg_icmp_code_(context->storage->InternString("packet_icmp_code")),
      net_ipproto_tcp_(context->storage->InternString("IPPROTO_TCP")),
      net_ipproto_udp_(context->storage->InternString("IPPROTO_UDP")),
      net_ipproto_icmp_(context->storage->InternString("IPPROTO_ICMP")),
      net_ipproto_icmpv6_(context->storage->InternString("IPPROTO_ICMPV6")),
      packet_count_(context->storage->InternString("packet_count")) {
  RegisterForField(TracePacket::kNetworkPacketFieldNumber);
  RegisterForField(TracePacket::kNetworkPacketBundleFieldNumber);
}

ModuleResult NetworkTraceModule::TokenizePacket(
    const protos::pbzero::TracePacket::Decoder& decoder,
    TraceBlobView*,
    int64_t ts,
    RefPtr<PacketSequenceStateGeneration> state,
    uint32_t field_id) {
  if (field_id != TracePacket::kNetworkPacketBundleFieldNumber) {
    return ModuleResult::Ignored();
  }

  NetworkPacketBundle::Decoder evt(decoder.network_packet_bundle());

  ConstBytes context = evt.ctx();
  if (evt.has_iid()) {
    auto* interned = state->LookupInternedMessage<
        protos::pbzero::InternedData::kPacketContextFieldNumber,
        protos::pbzero::NetworkPacketContext>(evt.iid());
    if (!interned) {
      context_->storage->IncrementStats(stats::network_trace_intern_errors);
    } else {
      context = interned->ctx();
    }
  }

  if (evt.has_total_length()) {
    // Forward the bundle with (possibly de-interned) context.
    packet_buffer_->set_timestamp(static_cast<uint64_t>(ts));
    auto* event = packet_buffer_->set_network_packet_bundle();
    event->set_ctx()->AppendRawProtoBytes(context.data, context.size);
    event->set_total_length(evt.total_length());
    event->set_total_packets(evt.total_packets());
    event->set_total_duration(evt.total_duration());
    PushPacketBufferForSort(ts, state);
  } else {
    // Push a NetworkPacketEvent for each packet in the packed arrays.
    bool parse_error = false;
    auto length_iter = evt.packet_lengths(&parse_error);
    auto timestamp_iter = evt.packet_timestamps(&parse_error);
    if (parse_error) {
      context_->storage->IncrementStats(stats::network_trace_parse_errors);
      return ModuleResult::Handled();
    }

    for (; timestamp_iter && length_iter; ++timestamp_iter, ++length_iter) {
      int64_t real_ts = ts + static_cast<int64_t>(*timestamp_iter);
      packet_buffer_->set_timestamp(static_cast<uint64_t>(real_ts));
      auto* event = packet_buffer_->set_network_packet();
      event->AppendRawProtoBytes(context.data, context.size);
      event->set_length(*length_iter);
      PushPacketBufferForSort(real_ts, state);
    }
  }

  return ModuleResult::Handled();
}

void NetworkTraceModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kNetworkPacketFieldNumber:
      ParseNetworkPacketEvent(ts, decoder.network_packet());
      return;
    case TracePacket::kNetworkPacketBundleFieldNumber:
      ParseNetworkPacketBundle(ts, decoder.network_packet_bundle());
      return;
    default:
      break;
  }
}

void NetworkTraceModule::ParseGenericEvent(
    int64_t ts,
    int64_t dur,
    int64_t length,
    int64_t count,
    protos::pbzero::NetworkPacketEvent::Decoder& evt) {
  // Tracks are per interface and per direction.
  const char* direction;
  switch (evt.direction()) {
    case TrafficDirection::DIR_INGRESS:
      direction = "Received";
      break;
    case TrafficDirection::DIR_EGRESS:
      direction = "Transmitted";
      break;
    default:
      direction = "DIR_UNKNOWN";
      break;
  }
  StringId direction_id = context_->storage->InternString(direction);
  StringId iface = context_->storage->InternString(evt.network_interface());

  if (!loaded_package_names_) {
    loaded_package_names_ = true;
    const auto& package_list = context_->storage->package_list_table();
    for (auto row = package_list.IterateRows(); row; ++row) {
      package_names_.Insert(row.uid(), row.package_name());
    }
  }

  // Android stores the app id in the lower part of the uid. The actual uid will
  // be `user_id * kPerUserRange + app_id`. For package lookup, we want app id.
  uint32_t app_id = evt.uid() % kPerUserRange;

  // Event titles are the package name, if available.
  StringId slice_name = kNullStringId;
  if (evt.uid() > 0) {
    StringId* iter = package_names_.Find(app_id);
    if (iter != nullptr) {
      slice_name = *iter;
    }
  }

  // If the above fails, fall back to the uid.
  if (slice_name == kNullStringId) {
    base::StackString<32> title_str("uid=%" PRIu32, evt.uid());
    slice_name = context_->storage->InternString(title_str.string_view());
  }

  static constexpr auto kBlueprint = TrackCompressor::SliceBlueprint(
      "network_packets",
      tracks::Dimensions(tracks::StringDimensionBlueprint("net_interface"),
                         tracks::StringDimensionBlueprint("net_direction")),
      tracks::FnNameBlueprint(
          [](base::StringView network, base::StringView direction) {
            return base::StackString<64>("%.*s %.*s", int(network.size()),
                                         network.data(), int(direction.size()),
                                         direction.data());
          }));

  TrackId track_id = context_->track_compressor->InternScoped(
      kBlueprint, tracks::Dimensions(evt.network_interface(), direction), ts,
      dur);

  tables::AndroidNetworkPacketsTable::Row actual_row;
  actual_row.iface = iface;
  actual_row.direction = direction_id;
  actual_row.packet_transport = GetIpProto(evt);
  actual_row.packet_length = length;
  actual_row.packet_count = count;
  actual_row.socket_tag = evt.tag();
  actual_row.socket_uid = evt.uid();
  actual_row.socket_tag_str = context_->storage->InternString(
      base::StackString<16>("0x%x", evt.tag()).string_view());

  if (evt.has_local_port()) {
    actual_row.local_port = evt.local_port();
  }
  if (evt.has_remote_port()) {
    actual_row.remote_port = evt.remote_port();
  }
  if (evt.has_icmp_type()) {
    actual_row.packet_icmp_type = evt.icmp_type();
  }
  if (evt.has_icmp_code()) {
    actual_row.packet_icmp_code = evt.icmp_code();
  }
  if (evt.has_tcp_flags()) {
    actual_row.packet_tcp_flags = evt.tcp_flags();
    actual_row.packet_tcp_flags_str = context_->storage->InternString(
        GetTcpFlagMask(evt.tcp_flags()).string_view());
  }
  std::optional<SliceId> id = context_->slice_tracker->Scoped(
      ts, track_id, kNullStringId, slice_name, dur,
      [&](ArgsTracker::BoundInserter* i) {
        i->AddArg(net_arg_ip_proto_,
                  Variadic::String(actual_row.packet_transport));

        i->AddArg(net_arg_uid_, Variadic::Integer(evt.uid()));
        i->AddArg(net_arg_tag_, Variadic::String(actual_row.socket_tag_str));

        if (actual_row.packet_tcp_flags_str.has_value()) {
          i->AddArg(net_arg_tcp_flags_,
                    Variadic::String(*actual_row.packet_tcp_flags_str));
        }

        if (evt.has_local_port()) {
          i->AddArg(net_arg_local_port_, Variadic::Integer(evt.local_port()));
        }
        if (evt.has_remote_port()) {
          i->AddArg(net_arg_remote_port_, Variadic::Integer(evt.remote_port()));
        }
        if (evt.has_icmp_type()) {
          i->AddArg(net_arg_icmp_type_, Variadic::Integer(evt.icmp_type()));
        }
        if (evt.has_icmp_code()) {
          i->AddArg(net_arg_icmp_code_, Variadic::Integer(evt.icmp_code()));
        }
        i->AddArg(net_arg_length_, Variadic::Integer(length));
        i->AddArg(packet_count_, Variadic::Integer(count));
      });
  if (id) {
    auto* network_packets =
        context_->storage->mutable_android_network_packets_table();
    actual_row.id = *id;
    network_packets->Insert(actual_row);
  }
}

StringId NetworkTraceModule::GetIpProto(NetworkPacketEvent::Decoder& evt) {
  switch (evt.ip_proto()) {
    case kIpprotoTcp:
      return net_ipproto_tcp_;
    case kIpprotoUdp:
      return net_ipproto_udp_;
    case kIpprotoIcmp:
      return net_ipproto_icmp_;
    case kIpprotoIcmpv6:
      return net_ipproto_icmpv6_;
    default:
      return context_->storage->InternString(
          base::StackString<32>("IPPROTO (%u)", evt.ip_proto()).string_view());
  }
}

void NetworkTraceModule::ParseNetworkPacketEvent(int64_t ts, ConstBytes blob) {
  NetworkPacketEvent::Decoder event(blob);
  ParseGenericEvent(ts, /*dur=*/0, event.length(), /*count=*/1, event);
}

void NetworkTraceModule::ParseNetworkPacketBundle(int64_t ts, ConstBytes blob) {
  NetworkPacketBundle::Decoder event(blob);
  NetworkPacketEvent::Decoder ctx(event.ctx());
  auto dur = static_cast<int64_t>(event.total_duration());
  auto length = static_cast<int64_t>(event.total_length());

  // Any bundle that makes it through tokenization must be aggregated bundles
  // with total packets/total length.
  ParseGenericEvent(ts, dur, length, event.total_packets(), ctx);
}

void NetworkTraceModule::PushPacketBufferForSort(
    int64_t timestamp,
    RefPtr<PacketSequenceStateGeneration> state) {
  auto [vec, size] = packet_buffer_.SerializeAsUniquePtr();
  TraceBlobView tbv(TraceBlob::TakeOwnership(std::move(vec), size));
  module_context_->trace_packet_stream->Push(
      timestamp, TracePacketData{std::move(tbv), std::move(state)});
  packet_buffer_.Reset();
}

}  // namespace perfetto::trace_processor
