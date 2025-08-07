/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "perfetto/tracing/console_interceptor.h"

#include <stdarg.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/tracing/internal/track_event_internal.h"

#include "protos/perfetto/common/interceptor_descriptor.gen.h"
#include "protos/perfetto/config/data_source_config.gen.h"
#include "protos/perfetto/config/interceptor_config.gen.h"
#include "protos/perfetto/config/interceptors/console_config.gen.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "protos/perfetto/trace/track_event/process_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_descriptor.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

namespace perfetto {

// sRGB color.
struct ConsoleColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

namespace {

int g_output_fd_for_testing;

// Google Turbo colormap.
constexpr std::array<ConsoleColor, 16> kTurboColors = {{
    ConsoleColor{0x30, 0x12, 0x3b},
    ConsoleColor{0x40, 0x40, 0xa1},
    ConsoleColor{0x46, 0x6b, 0xe3},
    ConsoleColor{0x41, 0x93, 0xfe},
    ConsoleColor{0x28, 0xbb, 0xeb},
    ConsoleColor{0x17, 0xdc, 0xc2},
    ConsoleColor{0x32, 0xf1, 0x97},
    ConsoleColor{0x6d, 0xfd, 0x62},
    ConsoleColor{0xa4, 0xfc, 0x3b},
    ConsoleColor{0xcd, 0xeb, 0x34},
    ConsoleColor{0xed, 0xcf, 0x39},
    ConsoleColor{0xfd, 0xab, 0x33},
    ConsoleColor{0xfa, 0x7d, 0x20},
    ConsoleColor{0xea, 0x50, 0x0d},
    ConsoleColor{0xd0, 0x2f, 0x04},
    ConsoleColor{0xa9, 0x15, 0x01},
}};

constexpr size_t kHueBits = 4;
constexpr uint32_t kMaxHue = kTurboColors.size() << kHueBits;
constexpr uint8_t kLightness = 128u;
constexpr ConsoleColor kWhiteColor{0xff, 0xff, 0xff};

const char kDim[] = "\x1b[90m";
const char kDefault[] = "\x1b[39m";
const char kReset[] = "\x1b[0m";

#define FMT_RGB_SET "\x1b[38;2;%d;%d;%dm"
#define FMT_RGB_SET_BG "\x1b[48;2;%d;%d;%dm"

ConsoleColor Mix(ConsoleColor a, ConsoleColor b, uint8_t ratio) {
  return {
      static_cast<uint8_t>(a.r + (((b.r - a.r) * ratio) >> 8)),
      static_cast<uint8_t>(a.g + (((b.g - a.g) * ratio) >> 8)),
      static_cast<uint8_t>(a.b + (((b.b - a.b) * ratio) >> 8)),
  };
}

ConsoleColor HueToRGB(uint32_t hue) {
  PERFETTO_DCHECK(hue < kMaxHue);
  uint32_t c1 = hue >> kHueBits;
  uint32_t c2 =
      std::min(static_cast<uint32_t>(kTurboColors.size() - 1), c1 + 1u);
  uint32_t ratio = hue & ((1 << kHueBits) - 1);
  return Mix(kTurboColors[c1], kTurboColors[c2],
             static_cast<uint8_t>(ratio | (ratio << kHueBits)));
}

uint32_t CounterToHue(uint32_t counter) {
  // We split the hue space into 8 segments, reversing the order of bits so
  // successive counter values will be far from each other.
  uint32_t reversed =
      ((counter & 0x7) >> 2) | ((counter & 0x3)) | ((counter & 0x1) << 2);
  return reversed * kMaxHue / 8;
}

}  // namespace

class ConsoleInterceptor::Delegate : public TrackEventStateTracker::Delegate {
 public:
  explicit Delegate(InterceptorContext&);
  ~Delegate() override;

  TrackEventStateTracker::SessionState* GetSessionState() override;
  void OnTrackUpdated(TrackEventStateTracker::Track&) override;
  void OnTrackEvent(const TrackEventStateTracker::Track&,
                    const TrackEventStateTracker::ParsedTrackEvent&) override;

 private:
  using SelfHandle = LockedHandle<ConsoleInterceptor>;

  InterceptorContext& context_;
  std::optional<SelfHandle> locked_self_;
};

ConsoleInterceptor::~ConsoleInterceptor() = default;

ConsoleInterceptor::ThreadLocalState::ThreadLocalState(
    ThreadLocalStateArgs& args) {
  if (auto self = args.GetInterceptorLocked()) {
    start_time_ns = self->start_time_ns_;
    use_colors = self->use_colors_;
    fd = self->fd_;
  }
}

ConsoleInterceptor::ThreadLocalState::~ThreadLocalState() = default;

ConsoleInterceptor::Delegate::Delegate(InterceptorContext& context)
    : context_(context) {}
ConsoleInterceptor::Delegate::~Delegate() = default;

TrackEventStateTracker::SessionState*
ConsoleInterceptor::Delegate::GetSessionState() {
  // When the session state is retrieved for the first time, it is cached (and
  // kept locked) until we return from OnTracePacket. This avoids having to lock
  // and unlock the instance multiple times per invocation.
  if (locked_self_.has_value())
    return &locked_self_.value()->session_state_;
  locked_self_ =
      std::make_optional<SelfHandle>(context_.GetInterceptorLocked());
  return &locked_self_.value()->session_state_;
}

void ConsoleInterceptor::Delegate::OnTrackUpdated(
    TrackEventStateTracker::Track& track) {
  auto track_color = HueToRGB(CounterToHue(track.index));
  std::array<char, 16> title;
  if (!track.name.empty()) {
    snprintf(title.data(), title.size(), "%s", track.name.c_str());
  } else if (track.pid && track.tid) {
    snprintf(title.data(), title.size(), "%u:%u",
             static_cast<uint32_t>(track.pid),
             static_cast<uint32_t>(track.tid));
  } else if (track.pid) {
    snprintf(title.data(), title.size(), "%" PRId64, track.pid);
  } else {
    snprintf(title.data(), title.size(), "%" PRIu64, track.uuid);
  }
  int title_width = static_cast<int>(title.size());

  auto& tls = context_.GetThreadLocalState();
  std::array<char, 128> message_prefix{};
  size_t written = 0;
  if (tls.use_colors) {
    written = base::SprintfTrunc(message_prefix.data(), message_prefix.size(),
                                 FMT_RGB_SET_BG " %s%s %-*.*s", track_color.r,
                                 track_color.g, track_color.b, kReset, kDim,
                                 title_width, title_width, title.data());
  } else {
    written = base::SprintfTrunc(message_prefix.data(), message_prefix.size(),
                                 "%-*.*s", title_width + 2, title_width,
                                 title.data());
  }
  track.user_data.assign(
      message_prefix.begin(),
      message_prefix.begin() + static_cast<ssize_t>(written));
}

void ConsoleInterceptor::Delegate::OnTrackEvent(
    const TrackEventStateTracker::Track& track,
    const TrackEventStateTracker::ParsedTrackEvent& event) {
  // Start printing.
  auto& tls = context_.GetThreadLocalState();
  tls.buffer_pos = 0;

  // Print timestamp and track identifier.
  SetColor(context_, kDim);
  Printf(context_, "[%7.3lf] %.*s",
         static_cast<double>(event.timestamp_ns - tls.start_time_ns) / 1e9,
         static_cast<int>(track.user_data.size()), track.user_data.data());

  // Print category.
  Printf(context_, "%-5.*s ",
         std::min(5, static_cast<int>(event.category.size)),
         event.category.data);

  // Print stack depth.
  for (size_t i = 0; i < event.stack_depth; i++) {
    Printf(context_, "-  ");
  }

  // Print slice name.
  auto slice_color = HueToRGB(event.name_hash % kMaxHue);
  auto highlight_color = Mix(slice_color, kWhiteColor, kLightness);
  if (event.track_event.type() == protos::pbzero::TrackEvent::TYPE_SLICE_END) {
    SetColor(context_, kDefault);
    Printf(context_, "} ");
  }
  SetColor(context_, highlight_color);
  Printf(context_, "%.*s", static_cast<int>(event.name.size), event.name.data);
  SetColor(context_, kReset);
  if (event.track_event.type() ==
      protos::pbzero::TrackEvent::TYPE_SLICE_BEGIN) {
    SetColor(context_, kDefault);
    Printf(context_, " {");
  }

  // Print annotations.
  if (event.track_event.has_debug_annotations()) {
    PrintDebugAnnotations(context_, event.track_event, slice_color,
                          highlight_color);
  }

  // TODO(skyostil): Print typed arguments.

  // Print duration for longer events.
  constexpr uint64_t kNsPerMillisecond = 1000000u;
  if (event.duration_ns >= 10 * kNsPerMillisecond) {
    SetColor(context_, kDim);
    Printf(context_, " +%" PRIu64 "ms", event.duration_ns / kNsPerMillisecond);
  }
  SetColor(context_, kReset);
  Printf(context_, "\n");
}

// static
void ConsoleInterceptor::Register() {
  perfetto::protos::gen::InterceptorDescriptor desc;
  desc.set_name("console");
  Interceptor<ConsoleInterceptor>::Register(desc);
}

// static
void ConsoleInterceptor::SetOutputFdForTesting(int fd) {
  g_output_fd_for_testing = fd;
}

void ConsoleInterceptor::OnSetup(const SetupArgs& args) {
  int fd = STDOUT_FILENO;
  if (g_output_fd_for_testing)
    fd = g_output_fd_for_testing;
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
  bool use_colors = isatty(fd);
#else
  bool use_colors = false;
#endif
  const protos::gen::ConsoleConfig& config =
      args.config.interceptor_config().console_config();
  if (config.has_enable_colors())
    use_colors = config.enable_colors();
  if (config.output() == protos::gen::ConsoleConfig::OUTPUT_STDOUT) {
    fd = STDOUT_FILENO;
  } else if (config.output() == protos::gen::ConsoleConfig::OUTPUT_STDERR) {
    fd = STDERR_FILENO;
  }
  fd_ = fd;
  use_colors_ = use_colors;
}

void ConsoleInterceptor::OnStart(const StartArgs&) {
  start_time_ns_ = internal::TrackEventInternal::GetTimeNs();
}

void ConsoleInterceptor::OnStop(const StopArgs&) {}

// static
void ConsoleInterceptor::OnTracePacket(InterceptorContext context) {
  {
    auto& tls = context.GetThreadLocalState();
    Delegate delegate(context);
    perfetto::protos::pbzero::TracePacket::Decoder packet(
        context.packet_data.data, context.packet_data.size);
    TrackEventStateTracker::ProcessTracePacket(delegate, tls.sequence_state,
                                               packet);
  }  // (Potential) lock scope for session state.
  Flush(context);
}

// static
void ConsoleInterceptor::Printf(InterceptorContext& context,
                                const char* format,
                                ...) {
  auto& tls = context.GetThreadLocalState();
  ssize_t remaining = static_cast<ssize_t>(tls.message_buffer.size()) -
                      static_cast<ssize_t>(tls.buffer_pos);
  int written = 0;
  if (remaining > 0) {
    va_list args;
    va_start(args, format);
    written = vsnprintf(&tls.message_buffer[tls.buffer_pos],
                        static_cast<size_t>(remaining), format, args);
    PERFETTO_DCHECK(written >= 0);
    va_end(args);
  }

  // In case of buffer overflow, flush to the fd and write the latest message to
  // it directly instead.
  if (remaining <= 0 || written > remaining) {
    FILE* output = (tls.fd == STDOUT_FILENO) ? stdout : stderr;
    if (g_output_fd_for_testing) {
      output = fdopen(dup(g_output_fd_for_testing), "w");
    }
    Flush(context);
    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    if (g_output_fd_for_testing) {
      fclose(output);
    }
  } else if (written > 0) {
    tls.buffer_pos += static_cast<size_t>(written);
  }
}

// static
void ConsoleInterceptor::Flush(InterceptorContext& context) {
  auto& tls = context.GetThreadLocalState();
  ssize_t res = base::WriteAll(tls.fd, &tls.message_buffer[0], tls.buffer_pos);
  PERFETTO_DCHECK(res == static_cast<ssize_t>(tls.buffer_pos));
  tls.buffer_pos = 0;
}

// static
void ConsoleInterceptor::SetColor(InterceptorContext& context,
                                  const ConsoleColor& color) {
  auto& tls = context.GetThreadLocalState();
  if (!tls.use_colors)
    return;
  Printf(context, FMT_RGB_SET, color.r, color.g, color.b);
}

// static
void ConsoleInterceptor::SetColor(InterceptorContext& context,
                                  const char* color) {
  auto& tls = context.GetThreadLocalState();
  if (!tls.use_colors)
    return;
  Printf(context, "%s", color);
}

// static
void ConsoleInterceptor::PrintDebugAnnotations(
    InterceptorContext& context,
    const protos::pbzero::TrackEvent_Decoder& track_event,
    const ConsoleColor& slice_color,
    const ConsoleColor& highlight_color) {
  SetColor(context, slice_color);
  Printf(context, "(");

  bool is_first = true;
  for (auto it = track_event.debug_annotations(); it; it++) {
    perfetto::protos::pbzero::DebugAnnotation::Decoder annotation(*it);
    SetColor(context, slice_color);
    if (!is_first)
      Printf(context, ", ");

    PrintDebugAnnotationName(context, annotation);
    Printf(context, ":");

    SetColor(context, highlight_color);
    PrintDebugAnnotationValue(context, annotation);

    is_first = false;
  }
  SetColor(context, slice_color);
  Printf(context, ")");
}

// static
void ConsoleInterceptor::PrintDebugAnnotationName(
    InterceptorContext& context,
    const perfetto::protos::pbzero::DebugAnnotation::Decoder& annotation) {
  auto& tls = context.GetThreadLocalState();
  protozero::ConstChars name{};
  if (annotation.name_iid()) {
    name.data =
        tls.sequence_state.debug_annotation_names[annotation.name_iid()].data();
    name.size =
        tls.sequence_state.debug_annotation_names[annotation.name_iid()].size();
  } else if (annotation.has_name()) {
    name.data = annotation.name().data;
    name.size = annotation.name().size;
  }
  Printf(context, "%.*s", static_cast<int>(name.size), name.data);
}

// static
void ConsoleInterceptor::PrintDebugAnnotationValue(
    InterceptorContext& context,
    const perfetto::protos::pbzero::DebugAnnotation::Decoder& annotation) {
  if (annotation.has_bool_value()) {
    Printf(context, "%s", annotation.bool_value() ? "true" : "false");
  } else if (annotation.has_uint_value()) {
    Printf(context, "%" PRIu64, annotation.uint_value());
  } else if (annotation.has_int_value()) {
    Printf(context, "%" PRId64, annotation.int_value());
  } else if (annotation.has_double_value()) {
    Printf(context, "%f", annotation.double_value());
  } else if (annotation.has_string_value()) {
    Printf(context, "%.*s", static_cast<int>(annotation.string_value().size),
           annotation.string_value().data);
  } else if (annotation.has_pointer_value()) {
    Printf(context, "%p", reinterpret_cast<void*>(annotation.pointer_value()));
  } else if (annotation.has_legacy_json_value()) {
    Printf(context, "%.*s",
           static_cast<int>(annotation.legacy_json_value().size),
           annotation.legacy_json_value().data);
  } else if (annotation.has_dict_entries()) {
    Printf(context, "{");
    bool is_first = true;
    for (auto it = annotation.dict_entries(); it; ++it) {
      if (!is_first)
        Printf(context, ", ");
      perfetto::protos::pbzero::DebugAnnotation::Decoder key_value(*it);
      PrintDebugAnnotationName(context, key_value);
      Printf(context, ":");
      PrintDebugAnnotationValue(context, key_value);
      is_first = false;
    }
    Printf(context, "}");
  } else if (annotation.has_array_values()) {
    Printf(context, "[");
    bool is_first = true;
    for (auto it = annotation.array_values(); it; ++it) {
      if (!is_first)
        Printf(context, ", ");
      perfetto::protos::pbzero::DebugAnnotation::Decoder key_value(*it);
      PrintDebugAnnotationValue(context, key_value);
      is_first = false;
    }
    Printf(context, "]");
  } else {
    Printf(context, "{}");
  }
}

}  // namespace perfetto
