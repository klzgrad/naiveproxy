/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <string>

#include "src/base/test/status_matchers.h"
#include "src/trace_redaction/broadphase_packet_filter.h"
#include "src/trace_redaction/populate_allow_lists.h"
#include "src/trace_redaction/trace_redaction_integration_fixture.h"
#include "test/gtest_and_gmock.h"

#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

class BroadphasePacketFilterIntegrationTest
    : public testing::Test,
      protected TraceRedactionIntegrationFixure {
 protected:
  void SetUp() override {
    trace_redactor_.emplace_build<PopulateAllowlists>();
    trace_redactor_.emplace_transform<BroadphasePacketFilter>();
  }

  Context::TracePacketMask ScanPacketFields(const std::string& trace) {
    protos::pbzero::Trace::Decoder trace_decoder(trace);

    Context::TracePacketMask mask;

    for (auto packet = trace_decoder.packet(); packet; ++packet) {
      protozero::ProtoDecoder decoder(*packet);

      for (auto field = decoder.ReadField(); field.valid();
           field = decoder.ReadField()) {
        PERFETTO_DCHECK(field.id() < mask.size());
        mask.set(field.id());
      }
    }

    return mask;
  }

  Context::FtraceEventMask ScanFtraceEventFields(const std::string& buffer) {
    protos::pbzero::Trace::Decoder trace(buffer);

    Context::FtraceEventMask mask;

    for (auto packet = trace.packet(); packet; ++packet) {
      protos::pbzero::TracePacket::Decoder decoder(*packet);

      if (decoder.has_ftrace_events()) {
        mask |= CopyEventFields(decoder.ftrace_events());
      }
    }

    return mask;
  }

  Context context_;
  TraceRedactor trace_redactor_;

 private:
  Context::FtraceEventMask CopyEventFields(protozero::ConstBytes bytes) {
    protozero::ProtoDecoder decoder(bytes);

    Context::FtraceEventMask mask;

    for (auto field = decoder.ReadField(); field.valid();
         field = decoder.ReadField()) {
      PERFETTO_DCHECK(field.id() < mask.size());
      mask.set(field.id());
    }

    return mask;
  }
};

// To avoid being fragile, this test checks that some included fields passed
// through redaction and checks that no excluded fields passed through
// redaction.
TEST_F(BroadphasePacketFilterIntegrationTest, OnlyKeepsIncludedPacketFields) {
  ASSERT_OK(Redact(trace_redactor_, &context_));

  auto trace = LoadRedacted();
  ASSERT_OK(trace);

  auto include_mask = context_.packet_mask;
  auto exclude_mask = ~include_mask;

  auto fields = ScanPacketFields(*trace);

  ASSERT_TRUE(fields.any());
  ASSERT_TRUE(include_mask.any());

  ASSERT_TRUE((fields & include_mask).any());
  ASSERT_FALSE((fields & exclude_mask).any());
}

// To avoid being fragile, this test checks that some included fields passed
// through redaction and checks that no excluded fields passed through
// redaction.
TEST_F(BroadphasePacketFilterIntegrationTest,
       OnlyKeepsIncludedFtraceEventFields) {
  ASSERT_OK(Redact(trace_redactor_, &context_));

  auto trace = LoadRedacted();
  ASSERT_OK(trace);

  auto include_mask = context_.ftrace_mask;
  auto exclude_mask = ~include_mask;

  auto fields = ScanFtraceEventFields(*trace);

  ASSERT_TRUE(fields.any());
  ASSERT_TRUE(include_mask.any());

  ASSERT_TRUE((fields & include_mask).any());
  ASSERT_FALSE((fields & exclude_mask).any());
}

}  // namespace perfetto::trace_redaction
