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

#ifndef SRC_ANDROID_SDK_PERFETTO_SDK_FOR_JNI_TRACING_SDK_H_
#define SRC_ANDROID_SDK_PERFETTO_SDK_FOR_JNI_TRACING_SDK_H_

#include <stdint.h>

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

#include "perfetto/public/abi/track_event_hl_abi.h"
#include "perfetto/public/tracing_session.h"
#include "perfetto/public/track_event.h"

// Macro copied from
// https://source.corp.google.com/h/googleplex-android/platform/superproject/main/+/main:system/libbase/include/android-base/macros.h;l=45;drc=bd641075ad60ed703baf59f63a9153d96d96b98e
// A macro to disallow the copy constructor and operator= functions
// This must be placed in the private: declarations for a class.
//
// For disallowing only assign or copy, delete the relevant operator or
// constructor, for example:
// void operator=(const TypeName&) = delete;
// Note, that most uses of DISALLOW_ASSIGN and DISALLOW_COPY are broken
// semantically, one should either use disallow both or neither. Try to
// avoid these in new code.
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete

/**
 * The objects declared here are intended to be managed by Java.
 * This means the Java Garbage Collector is responsible for freeing the
 * underlying native resources.
 *
 * The static methods prefixed with `delete_` are special. They are designed to
 * be invoked by Java through the `NativeAllocationRegistry` when the
 * corresponding Java object becomes unreachable.  These methods act as
 * callbacks to ensure proper deallocation of native resources.
 */
namespace perfetto {
namespace sdk_for_jni {
/**
 * @brief Initializes the global perfetto instance.
 * @param backend_in_process use in-process or system backend
 */
void register_perfetto(bool backend_in_process = false);

/**
 * @brief Represents extra data associated with a trace event.
 * This class manages a collection of PerfettoTeHlExtra pointers.
 */
class Extra;

/**
 * @brief Emits a trace event.
 * @param type The type of the event.
 * @param cat The category of the event.
 * @param name The name of the event.
 * @param extra Pointer to Extra data.
 */
void trace_event(int type,
                 const PerfettoTeCategory* cat,
                 const char* name,
                 Extra* extra);

/**
 * @brief Gets the process track UUID.
 */
uint64_t get_process_track_uuid();

/**
 * @brief Gets the thread track UUID for a given PID.
 */
uint64_t get_thread_track_uuid(pid_t tid);

/**
 * @brief Holder for all the other classes in the file.
 */
class Extra {
 public:
  Extra();
  void push_extra(PerfettoTeHlExtra* extra);
  void pop_extra();
  void clear_extras();
  static void delete_extra(Extra* extra);

  PerfettoTeHlExtra* const* get() const { return extras_.data(); }

 private:
  DISALLOW_COPY_AND_ASSIGN(Extra);

  // These PerfettoTeHlExtra pointers are really pointers to all the other
  // types of extras: Category, DebugArg, Counter etc. Those objects are
  // individually managed by Java.
  std::vector<PerfettoTeHlExtra*> extras_;
};

/**
 * @brief Represents a trace event category.
 */
class Category {
 public:
  explicit Category(const std::string& name);

  Category(const std::string& name, const std::vector<std::string>& tags);

  ~Category();

  void register_category();

  void unregister_category();

  bool is_category_enabled();

  static void delete_category(Category* category);

  const PerfettoTeCategory* get() const { return &category_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Category);
  PerfettoTeCategory category_;
  const std::string name_;
  const std::vector<std::string> tags_;
  std::vector<const char*> tags_data_;
};

/**
 * @brief Represents one end of a flow between two events.
 */
class Flow {
 public:
  Flow();

  void set_process_flow(uint64_t id);
  void set_process_terminating_flow(uint64_t id);
  static void delete_flow(Flow* flow);

  const PerfettoTeHlExtraFlow* get() const { return &flow_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Flow);
  PerfettoTeHlExtraFlow flow_;
};

/**
 * @brief Represents a named track.
 */
class NamedTrack {
 public:
  NamedTrack(uint64_t id, uint64_t parent_uuid, const std::string& name);

  static void delete_track(NamedTrack* track);

  const PerfettoTeHlExtraNamedTrack* get() const { return &track_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(NamedTrack);
  const std::string name_;
  PerfettoTeHlExtraNamedTrack track_;
};

/**
 * @brief Represents a registered track.
 */
class RegisteredTrack {
 public:
  RegisteredTrack(uint64_t id,
                  uint64_t parent_uuid,
                  const std::string& name,
                  bool is_counter);
  ~RegisteredTrack();

  void register_track();
  void unregister_track();
  static void delete_track(RegisteredTrack* track);

  const PerfettoTeHlExtraRegisteredTrack* get() const { return &track_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(RegisteredTrack);
  PerfettoTeRegisteredTrack registered_track_;
  PerfettoTeHlExtraRegisteredTrack track_;
  const std::string name_;
  const uint64_t id_;
  const uint64_t parent_uuid_;
  const bool is_counter_;
};

/**
 * @brief Represents a counter track event.
 */
class Counter {
 public:
  Counter() {}

  static void delete_counter(Counter* counter) { delete counter; }

  PerfettoTeHlExtraCounterUnion* get() { return &counter_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Counter);
  PerfettoTeHlExtraCounterUnion counter_;
};

/**
 * @brief Represents a debug argument for a trace event.
 */
class DebugArg {
 public:
  explicit DebugArg(const std::string& name) : name_(name) {}

  static void delete_arg(DebugArg* arg) { delete arg; }

  const char* name() const { return name_.c_str(); }
  PerfettoTeHlExtraDebugArgUnion* get() { return &arg_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(DebugArg);
  PerfettoTeHlExtraDebugArgUnion arg_;
  const std::string name_;
};

class ProtoField {
 public:
  ProtoField() {}
  static void delete_field(ProtoField* field) { delete field; }

  PerfettoTeHlProtoFieldUnion* get() { return &arg_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(ProtoField);
  PerfettoTeHlProtoFieldUnion arg_;
};

class ProtoFieldNested {
 public:
  ProtoFieldNested();

  void add_field(PerfettoTeHlProtoField* field);
  void set_id(uint32_t id);
  static void delete_field(ProtoFieldNested* field);

  const PerfettoTeHlProtoFieldNested* get() const { return &field_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(ProtoFieldNested);
  PerfettoTeHlProtoFieldNested field_;
  // These PerfettoTeHlProtoField pointers are really pointers to all the other
  // types of protos: PerfettoTeHlProtoFieldVarInt,
  // PerfettoTeHlProtoFieldVarInt, PerfettoTeHlProtoFieldVarInt,
  // PerfettoTeHlProtoFieldNested. Those objects are individually managed by
  // Java.
  std::vector<PerfettoTeHlProtoField*> fields_;
};

class Proto {
 public:
  Proto();

  void add_field(PerfettoTeHlProtoField* field);
  void clear_fields();
  static void delete_proto(Proto* proto);

  const PerfettoTeHlExtraProtoFields* get() const { return &proto_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Proto);
  PerfettoTeHlExtraProtoFields proto_;
  // These PerfettoTeHlProtoField pointers are really pointers to all the other
  // types of protos: PerfettoTeHlProtoFieldVarInt,
  // PerfettoTeHlProtoFieldVarInt, PerfettoTeHlProtoFieldVarInt,
  // PerfettoTeHlProtoFieldNested. Those objects are individually managed by
  // Java.
  std::vector<PerfettoTeHlProtoField*> fields_;
};

class Session {
 public:
  Session(bool is_backend_in_process, void* buf, size_t len);
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  bool FlushBlocking(uint32_t timeout_ms);
  void StopBlocking();
  std::vector<uint8_t> ReadBlocking();

  static void delete_session(Session* session);

  struct PerfettoTracingSessionImpl* session_ = nullptr;
};

/**
 * @brief Activates a trigger.
 * @param name The name of the trigger.
 * @param ttl_ms The time-to-live of the trigger in milliseconds.
 */
void activate_trigger(const char* name, uint32_t ttl_ms);
}  // namespace sdk_for_jni
}  // namespace perfetto

#endif  // SRC_ANDROID_SDK_PERFETTO_SDK_FOR_JNI_TRACING_SDK_H_
