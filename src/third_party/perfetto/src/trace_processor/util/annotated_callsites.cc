/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/trace_processor/util/annotated_callsites.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

AnnotatedCallsites::AnnotatedCallsites(const TraceProcessorContext* context)
    : context_(*context),
      // String to identify trampoline frames. If the string does not exist in
      // TraceProcessor's StringPool (nullopt) then there will be no trampoline
      // frames in the trace so there is no point in adding it to the pool to do
      // all comparisons, instead we initialize the member to std::nullopt and
      // the string comparisons will all fail.
      art_jni_trampoline_(
          context->storage->string_pool().GetId("art_jni_trampoline")) {}

AnnotatedCallsites::State AnnotatedCallsites::GetState(
    std::optional<CallsiteId> id) {
  if (!id) {
    return State::kInitial;
  }
  auto it = states_.find(*id);
  if (it != states_.end()) {
    return it->second;
  }

  State state =
      Get(*context_.storage->stack_profile_callsite_table().FindById(*id))
          .first;
  states_.emplace(*id, state);
  return state;
}

std::pair<AnnotatedCallsites::State, CallsiteAnnotation>
AnnotatedCallsites::Get(
    const tables::StackProfileCallsiteTable::ConstRowReference& callsite) {
  State state = GetState(callsite.parent_id());

  // Keep immediate callee of a JNI trampoline, but keep tagging all
  // successive libart frames as common.
  if (state == State::kKeepNext) {
    return {State::kEraseLibart, CallsiteAnnotation::kNone};
  }

  // Special-case "art_jni_trampoline" frames, keeping their immediate callee
  // even if it is in libart, as it could be a native implementation of a
  // managed method. Example for "java.lang.reflect.Method.Invoke":
  //   art_jni_trampoline
  //   art::Method_invoke(_JNIEnv*, _jobject*, _jobject*, _jobjectArray*)
  //
  // Simpleperf also relies on this frame name, so it should be fairly stable.
  // TODO(rsavitski): consider detecting standard JNI upcall entrypoints -
  // _JNIEnv::Call*. These are sometimes inlined into other DSOs, so erasing
  // only the libart frames does not clean up all of the JNI-related frames.
  auto frame = *context_.storage->stack_profile_frame_table().FindById(
      callsite.frame_id());
  // art_jni_trampoline_ could be std::nullopt if the string does not exist in
  // the StringPool, but that also means no frame will ever have that name.
  if (art_jni_trampoline_.has_value() &&
      frame.name() == art_jni_trampoline_.value()) {
    return {State::kKeepNext, CallsiteAnnotation::kCommonFrame};
  }

  MapType map_type = GetMapType(frame.mapping());

  // Annotate managed frames.
  if (map_type == MapType::kArtInterp ||  //
      map_type == MapType::kArtJit ||     //
      map_type == MapType::kArtAot) {
    // Now know to be in a managed callstack - erase subsequent ART frames.
    if (state == State::kInitial) {
      state = State::kEraseLibart;
    }

    if (map_type == MapType::kArtInterp)
      return {state, CallsiteAnnotation::kArtInterpreted};
    if (map_type == MapType::kArtJit)
      return {state, CallsiteAnnotation::kArtJit};
    if (map_type == MapType::kArtAot)
      return {state, CallsiteAnnotation::kArtAot};
  }

  // Mixed callstack, tag libart frames as uninteresting (common-frame).
  // Special case a subset of interpreter implementation frames as
  // "common-frame-interp" using frame name prefixes. Those functions are
  // actually executed, whereas the managed "interp" frames are synthesised as
  // their caller by the unwinding library (based on the dex_pc virtual
  // register restored using the libart's DWARF info). The heuristic covers
  // the "nterp" and "switch" interpreter implementations.
  //
  // Example:
  //  <towards root>
  //  android.view.WindowLayout.computeFrames [interp]
  //  nterp_op_iget_object_slow_path [common-frame-interp]
  //
  // This annotation is helpful when trying to answer "what mode was the
  // process in?" based on the leaf frame of the callstack. As we want to
  // classify such cases as interpreted, even though the leaf frame is
  // libart.so.
  //
  // For "switch" interpreter, we match any frame starting with
  // "art::interpreter::" according to itanium mangling.
  if (state == State::kEraseLibart && map_type == MapType::kNativeLibart) {
    NullTermStringView fname = context_.storage->GetString(frame.name());
    if (fname.StartsWith("nterp_") || fname.StartsWith("Nterp") ||
        fname.StartsWith("ExecuteNterp") ||
        fname.StartsWith("ExecuteSwitchImpl") ||
        fname.StartsWith("_ZN3art11interpreter")) {
      return {state, CallsiteAnnotation::kCommonFrameInterp};
    }
    return {state, CallsiteAnnotation::kCommonFrame};
  }

  return {state, CallsiteAnnotation::kNone};
}

AnnotatedCallsites::MapType AnnotatedCallsites::GetMapType(MappingId id) {
  auto it = map_types_.find(id);
  if (it != map_types_.end()) {
    return it->second;
  }

  return map_types_
      .emplace(id, ClassifyMap(context_.storage->GetString(
                       context_.storage->stack_profile_mapping_table()
                           .FindById(id)
                           ->name())))
      .first->second;
}

AnnotatedCallsites::MapType AnnotatedCallsites::ClassifyMap(
    NullTermStringView map) {
  if (map.empty())
    return MapType::kOther;

  // Primary mapping where modern ART puts jitted code.
  // The Zygote's JIT region is inherited by all descendant apps, so it can
  // still appear in their callstacks.
  if (map.StartsWith("/memfd:jit-cache") ||
      map.StartsWith("/memfd:jit-zygote-cache")) {
    return MapType::kArtJit;
  }

  size_t last_slash_pos = map.rfind('/');
  if (last_slash_pos != NullTermStringView::npos) {
    base::StringView suffix = map.substr(last_slash_pos);
    if (suffix.StartsWith("/libart.so") || suffix.StartsWith("/libartd.so"))
      return MapType::kNativeLibart;
  }

  size_t extension_pos = map.rfind('.');
  if (extension_pos != NullTermStringView::npos) {
    base::StringView suffix = map.substr(extension_pos);
    if (suffix.StartsWith(".so"))
      return MapType::kNativeOther;
    // unqualified dex
    if (suffix.StartsWith(".dex"))
      return MapType::kArtInterp;
    // dex with verification speedup info, produced by dex2oat
    if (suffix.StartsWith(".vdex"))
      return MapType::kArtInterp;
    // possibly uncompressed dex in a jar archive
    if (suffix.StartsWith(".jar"))
      return MapType::kArtInterp;
    // android package (zip file), this can contain uncompressed dexes or
    // native libraries that are mmap'd directly into the process. We rely on
    // libunwindstack's MapInfo::GetFullName, which suffixes the mapping with
    // "!lib.so" if it knows that the referenced piece of the archive is an
    // uncompressed ELF file. So an unadorned ".apk" is assumed to be a dex
    // file.
    if (suffix.StartsWith(".apk"))
      return MapType::kArtInterp;
    // ahead of time compiled ELFs
    if (suffix.StartsWith(".oat"))
      return MapType::kArtAot;
    // older/alternative name for .oat
    if (suffix.StartsWith(".odex"))
      return MapType::kArtAot;
  }
  return MapType::kOther;
}

}  // namespace perfetto::trace_processor
