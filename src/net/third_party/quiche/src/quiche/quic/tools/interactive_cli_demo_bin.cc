// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// interactive_cli_demo -- a tool to debug InteractiveCli.

#include <memory>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/tools/interactive_cli.h"
#include "quiche/common/quiche_callbacks.h"

// A clock that outputs a counter every tick.
class CliClock : public quic::QuicAlarm::Delegate {
 public:
  using RearmCallback = quiche::MultiUseCallback<void()>;

  explicit CliClock(quic::InteractiveCli* cli) : cli_(cli) {}
  quic::QuicConnectionContext* GetConnectionContext() override {
    return nullptr;
  }
  void OnAlarm() override {
    cli_->PrintLine(absl::StrCat(counter_++));
    Rearm();
  }
  void Rearm() { rearm_callback_(); }
  void set_rearm_callback(RearmCallback callback) {
    rearm_callback_ = std::move(callback);
  }

 private:
  quic::InteractiveCli* cli_;
  int counter_ = 0;
  RearmCallback rearm_callback_;
};

int main(int argc, char** argv) {
  std::unique_ptr<quic::QuicEventLoop> event_loop =
      quic::GetDefaultEventLoop()->Create(quic::QuicDefaultClock::Get());
  std::unique_ptr<quic::QuicAlarmFactory> alarm_factory =
      event_loop->CreateAlarmFactory();

  quic::InteractiveCli cli(event_loop.get(), [&](absl::string_view line) {
    cli.PrintLine(absl::StrCat("Read line: ", absl::CEscape(line)));
  });
  CliClock clock(&cli);
  std::unique_ptr<quic::QuicAlarm> alarm =
      absl::WrapUnique(alarm_factory->CreateAlarm(&clock));
  clock.set_rearm_callback([&alarm] {
    alarm->Set(quic::QuicDefaultClock::Get()->Now() +
               quic::QuicTimeDelta::FromSeconds(1));
  });
  clock.Rearm();

  for (;;) {
    event_loop->RunEventLoopOnce(quic::QuicTimeDelta::FromSeconds(2));
  }
  return 0;
}
