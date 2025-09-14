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

#include "src/traceconv/trace_to_hprof.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/endian.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/traceconv/utils.h"

// Spec
// http://hg.openjdk.java.net/jdk6/jdk6/jdk/raw-file/tip/src/share/demo/jvmti/hprof/manual.html#Basic_Type
// Parser
// https://cs.android.com/android/platform/superproject/main/+/main:art/tools/ahat/src/main/com/android/ahat/heapdump/Parser.java

namespace perfetto {
namespace trace_to_text {

namespace {
constexpr char kHeader[] = "PERFETTO_JAVA_HEAP";
constexpr uint32_t kIdSz = 8;
constexpr uint32_t kStackTraceSerialNumber = 1;

class BigEndianBuffer {
 public:
  void WriteId(uint64_t val) { WriteU8(val); }

  void WriteU8(uint64_t val) {
    val = base::HostToBE64(val);
    Write(reinterpret_cast<char*>(&val), sizeof(uint64_t));
  }

  void WriteU4(uint32_t val) {
    val = base::HostToBE32(val);
    Write(reinterpret_cast<char*>(&val), sizeof(uint32_t));
  }

  void SetU4(uint32_t val, size_t pos) {
    val = base::HostToBE32(val);
    PERFETTO_CHECK(pos + 4 <= buf_.size());
    memcpy(buf_.data() + pos, &val, sizeof(uint32_t));
  }

  // Uncomment when needed
  // void WriteU2(uint16_t val) {
  //   val = base::HostToBE16(val);
  //   Write(reinterpret_cast<char*>(&val), sizeof(uint16_t));
  // }

  void WriteByte(uint8_t val) { buf_.emplace_back(val); }

  void Write(const char* val, uint32_t sz) {
    const char* end = val + sz;
    while (val < end) {
      WriteByte(static_cast<uint8_t>(*val));
      val++;
    }
  }

  size_t written() const { return buf_.size(); }

  void Flush(std::ostream* out) const {
    out->write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
  }

 private:
  std::vector<char> buf_;
};

class HprofWriter {
 public:
  HprofWriter(std::ostream* output) : output_(output) {}

  void WriteBuffer(const BigEndianBuffer& buf) { buf.Flush(output_); }

  void WriteRecord(const uint8_t type,
                   const std::function<void(BigEndianBuffer*)>&& writer) {
    BigEndianBuffer buf;
    buf.WriteByte(type);
    // ts offset
    buf.WriteU4(0);
    // size placeholder
    buf.WriteU4(0);
    writer(&buf);
    uint32_t record_sz = static_cast<uint32_t>(buf.written() - 9);
    buf.SetU4(record_sz, 5);
    WriteBuffer(buf);
  }

 private:
  std::ostream* output_;
};

// A Class from the heap dump.
class ClassData {
 public:
  explicit ClassData(uint64_t class_name_string_id)
      : class_name_string_id_(class_name_string_id) {}

  // Writes a HPROF LOAD_CLASS record for this Class
  void WriteHprofLoadClass(HprofWriter* writer,
                           uint64_t class_object_id,
                           uint32_t class_serial_number) const {
    writer->WriteRecord(0x02, [class_object_id, class_serial_number,
                               this](BigEndianBuffer* buf) {
      buf->WriteU4(class_serial_number);
      buf->WriteId(class_object_id);
      buf->WriteU4(kStackTraceSerialNumber);
      buf->WriteId(class_name_string_id_);
    });
  }

 private:
  uint64_t class_name_string_id_;
};

// Ingested data from a Java Heap Profile for a name, location pair.
// We need to support multiple class data per pair as name, location is
// not unique. Classloader should guarantee uniqueness but is not available
// until S.
class RawClassData {
 public:
  void AddClass(uint64_t id, std::optional<uint64_t> superclass_id) {
    ids_.push_back(std::make_pair(id, superclass_id));
  }

  void AddTemplate(uint64_t template_id) {
    template_ids_.push_back(template_id);
  }

  // Transforms the raw data into one or more ClassData and adds them to the
  // parameter map.
  void ToClassData(std::unordered_map<uint64_t, ClassData>* id_to_class,
                   uint64_t class_name_string_id) const {
    // TODO(dinoderek) assert the two vectors have same length, iterate on both
    for (auto it_ids = ids_.begin(); it_ids != ids_.end(); ++it_ids) {
      // TODO(dinoderek) more data will be needed to write CLASS_DUMP
      id_to_class->emplace(it_ids->first, ClassData(class_name_string_id));
    }
  }

 private:
  // Pair contains class ID and super class ID.
  std::vector<std::pair<uint64_t, std::optional<uint64_t>>> ids_;
  // Class id of the template
  std::vector<uint64_t> template_ids_;
};

// The Heap Dump data
class HeapDump {
 public:
  explicit HeapDump(trace_processor::TraceProcessor* tp) : tp_(tp) {}

  void Ingest() { IngestClasses(); }

  void Write(HprofWriter* writer) {
    WriteStrings(writer);
    WriteLoadClass(writer);
  }

 private:
  trace_processor::TraceProcessor* tp_;

  // String IDs start from 1 as 0 appears to be reserved.
  uint64_t next_string_id_ = 1;
  // Strings to corresponding String ID
  std::unordered_map<std::string, uint64_t> string_to_id_;
  // Type ID to corresponding Class
  std::unordered_map<uint64_t, ClassData> id_to_class_;

  // Ingests and processes the class data from the heap dump.
  void IngestClasses() {
    // TODO(dinoderek): heap_graph_class does not support pid or ts filtering

    std::map<std::pair<uint64_t, std::string>, RawClassData> raw_classes;

    auto it = tp_->ExecuteQuery(R"(SELECT
          id,
          IFNULL(deobfuscated_name, name),
          superclass_id,
          location
        FROM heap_graph_class )");

    while (it.Next()) {
      uint64_t id = static_cast<uint64_t>(it.Get(0).AsLong());

      std::string raw_dname(it.Get(1).AsString());
      std::string dname;
      bool is_template_class =
          base::StartsWith(raw_dname, std::string("java.lang.Class<"));
      if (is_template_class) {
        dname = raw_dname.substr(17, raw_dname.size() - 18);
      } else {
        dname = raw_dname;
      }
      uint64_t name_id = IngestString(dname);

      auto raw_super_id = it.Get(2);
      std::optional<uint64_t> maybe_super_id =
          raw_super_id.is_null()
              ? std::nullopt
              : std::optional<uint64_t>(
                    static_cast<uint64_t>(raw_super_id.AsLong()));

      std::string location(it.Get(3).AsString());

      auto raw_classes_it =
          raw_classes.emplace(std::make_pair(name_id, location), RawClassData())
              .first;
      if (is_template_class) {
        raw_classes_it->second.AddTemplate(id);
      } else {
        raw_classes_it->second.AddClass(id, maybe_super_id);
      }
    }

    for (const auto& raw : raw_classes) {
      auto class_name_string_id = raw.first.first;
      raw.second.ToClassData(&id_to_class_, class_name_string_id);
    }
  }

  // Ingests the parameter string and returns the HPROF ID for the string.
  uint64_t IngestString(const std::string& s) {
    auto maybe_id = string_to_id_.find(s);
    if (maybe_id != string_to_id_.end()) {
      return maybe_id->second;
    } else {
      auto id = next_string_id_;
      next_string_id_ += 1;
      string_to_id_[s] = id;
      return id;
    }
  }

  // Writes STRING sections to the output
  void WriteStrings(HprofWriter* writer) {
    for (const auto& it : string_to_id_) {
      writer->WriteRecord(0x01, [it](BigEndianBuffer* buf) {
        buf->WriteId(it.second);
        // TODO(dinoderek): UTF-8 encoding
        buf->Write(it.first.c_str(), static_cast<uint32_t>(it.first.length()));
      });
    }
  }

  // Writes LOAD CLASS sections to the output
  void WriteLoadClass(HprofWriter* writer) {
    uint32_t class_serial_number = 1;
    for (const auto& it : id_to_class_) {
      it.second.WriteHprofLoadClass(writer, it.first, class_serial_number);
      class_serial_number += 1;
    }
  }
};

void WriteHeaderAndStack(HprofWriter* writer) {
  BigEndianBuffer header;
  header.Write(kHeader, sizeof(kHeader));
  // Identifier size
  header.WriteU4(kIdSz);
  // walltime high (unused)
  header.WriteU4(0);
  // walltime low (unused)
  header.WriteU4(0);
  writer->WriteBuffer(header);

  // Add placeholder stack trace (required by the format).
  writer->WriteRecord(0x05, [](BigEndianBuffer* buf) {
    buf->WriteU4(kStackTraceSerialNumber);
    buf->WriteU4(0);
    buf->WriteU4(0);
  });
}
}  // namespace

int TraceToHprof(trace_processor::TraceProcessor* tp,
                 std::ostream* output,
                 uint64_t pid,
                 uint64_t ts) {
  PERFETTO_DCHECK(tp != nullptr && pid != 0 && ts != 0);

  HprofWriter writer(output);
  HeapDump dump(tp);

  dump.Ingest();
  WriteHeaderAndStack(&writer);
  dump.Write(&writer);

  return 0;
}

int TraceToHprof(std::istream* input,
                 std::ostream* output,
                 uint64_t pid,
                 std::vector<uint64_t> timestamps) {
  // TODO: Simplify this for cmdline users. For example, if there is a single
  // heap graph, use this, and only fail when there is ambiguity.
  if (pid == 0) {
    PERFETTO_ELOG("Must specify pid");
    return -1;
  }
  if (timestamps.size() != 1) {
    PERFETTO_ELOG("Must specify single timestamp");
    return -1;
  }
  trace_processor::Config config;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);
  if (!ReadTraceUnfinalized(tp.get(), input))
    return false;
  if (auto status = tp->NotifyEndOfFile(); !status.ok()) {
    return false;
  }
  return TraceToHprof(tp.get(), output, pid, timestamps[0]);
}

}  // namespace trace_to_text
}  // namespace perfetto
