// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_pump.h"

#include "base/message_loop/message_pump_default.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/message_loop/message_pump_for_ui.h"

#if defined(OS_MACOSX)
#include "base/message_loop/message_pump_mac.h"
#endif

namespace base {

namespace {

MessagePump::MessagePumpFactory* message_pump_for_ui_factory_ = nullptr;

}  // namespace

MessagePump::MessagePump() = default;

MessagePump::~MessagePump() = default;

void MessagePump::SetTimerSlack(TimerSlack) {
}

// static
void MessagePump::OverrideMessagePumpForUIFactory(MessagePumpFactory* factory) {
  DCHECK(!message_pump_for_ui_factory_);
  message_pump_for_ui_factory_ = factory;
}

// static
bool MessagePump::IsMessagePumpForUIFactoryOveridden() {
  return message_pump_for_ui_factory_ != nullptr;
}

// static
std::unique_ptr<MessagePump> MessagePump::Create(Type type) {
  switch (type) {
    case Type::UI:
      if (message_pump_for_ui_factory_)
        return message_pump_for_ui_factory_();
#if defined(OS_IOS) || defined(OS_MACOSX)
      return MessagePumpMac::Create();
#elif defined(OS_NACL) || defined(OS_AIX)
      // Currently NaCl and AIX don't have a UI MessagePump.
      // TODO(abarth): Figure out if we need this.
      NOTREACHED();
      return nullptr;
#else
      return std::make_unique<MessagePumpForUI>();
#endif

    case Type::IO:
      return std::make_unique<MessagePumpForIO>();

#if defined(OS_ANDROID)
    case Type::JAVA:
      return std::make_unique<MessagePumpForUI>();
#endif

#if defined(OS_MACOSX)
    case Type::NS_RUNLOOP:
      return std::make_unique<MessagePumpNSRunLoop>();
#endif

    case Type::CUSTOM:
      NOTREACHED();
      return nullptr;

    case Type::DEFAULT:
#if defined(OS_IOS)
      // On iOS, a native runloop is always required to pump system work.
      return std::make_unique<MessagePumpCFRunLoop>();
#else
      return std::make_unique<MessagePumpDefault>();
#endif
  }
}

}  // namespace base
