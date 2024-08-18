// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/interactive_cli.h"

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {
namespace {
// Writes into stdout.
void Write(absl::string_view data) {
  int written = write(STDOUT_FILENO, data.data(), data.size());
  QUICHE_DCHECK_EQ(written, data.size());
}
}  // namespace

InteractiveCli::InteractiveCli(QuicEventLoop* event_loop,
                               LineCallback line_callback)
    : event_loop_(event_loop), line_callback_(std::move(line_callback)) {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    QUICHE_LOG(FATAL) << "Both stdin and stdout must be a TTY";
  }

  [[maybe_unused]] bool success =
      event_loop_->RegisterSocket(STDIN_FILENO, kSocketEventReadable, this);
  QUICHE_LOG_IF(FATAL, !success)
      << "Failed to register stdin with the event loop";

  // Store old termios so that we can recover it when exiting.
  ::termios config;
  tcgetattr(STDIN_FILENO, &config);
  old_termios_ = std::make_unique<char[]>(sizeof(config));
  memcpy(old_termios_.get(), &config, sizeof(config));

  // Disable input buffering on the terminal.
  config.c_lflag &= ~(ICANON | ECHO | ECHONL);
  config.c_cc[VMIN] = 0;
  config.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &config);

  RestoreCurrentInputLine();
}

InteractiveCli::~InteractiveCli() {
  if (old_termios_ != nullptr) {
    tcsetattr(STDIN_FILENO, TCSANOW,
              reinterpret_cast<termios*>(old_termios_.get()));
  }
  [[maybe_unused]] bool success = event_loop_->UnregisterSocket(STDIN_FILENO);
  QUICHE_LOG_IF(ERROR, !success) << "Failed to unregister stdin";
}

void InteractiveCli::ResetLine() {
  constexpr absl::string_view kReset = "\033[G\033[K";
  Write(kReset);
}

void InteractiveCli::RestoreCurrentInputLine() {
  Write(absl::StrCat(prompt_, current_input_line_));
}

void InteractiveCli::PrintLine(absl::string_view line) {
  ResetLine();
  Write(absl::StrCat("\n\033[1A", absl::StripTrailingAsciiWhitespace(line),
                     "\n"));
  RestoreCurrentInputLine();
}

void InteractiveCli::OnSocketEvent(QuicEventLoop* event_loop, SocketFd fd,
                                   QuicSocketEventMask events) {
  QUICHE_DCHECK(events == kSocketEventReadable);

  std::string all_input;
  for (;;) {
    char buffer[1024];
    ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));
    // Since we set both VMIN and VTIME to zero, read() will return immediately
    // if there is nothing to read; see termios(3) for details.
    if (bytes_read <= 0) {
      if (bytes_read == 0) {
        break;
      }
      QUICHE_LOG(FATAL) << "Failed to read from stdin, errno: " << errno;
      return;
    }
    all_input.append(buffer, bytes_read);
  }

  if (!event_loop_->SupportsEdgeTriggered()) {
    (void)event_loop_->RearmSocket(STDIN_FILENO, kSocketEventReadable);
  }

  std::vector<absl::string_view> lines = absl::StrSplit(all_input, '\n');
  if (lines.empty()) {
    return;
  }
  if (lines.size() == 1) {
    // Usual case: there are no newlines.
    absl::StrAppend(&current_input_line_, lines.front());
  } else {
    // There could two (if user hit ENTER) or more (if user pastes things into
    // the terminal) lines; process all but the last one immediately.
    line_callback_(absl::StrCat(current_input_line_, lines.front()));
    current_input_line_.clear();

    for (int i = 1; i < lines.size() - 1; ++i) {
      line_callback_(lines[i]);
    }
    current_input_line_ = std::string(lines.back());
  }

  // Handle backspace.
  while (current_input_line_.size() >= 2 &&
         current_input_line_.back() == '\x7f') {
    current_input_line_.resize(current_input_line_.size() - 2);
  }
  // "Remove" escape sequences (it does not fully remove them, but gives the
  // user enough indication that those won't work).
  current_input_line_.erase(
      std::remove_if(current_input_line_.begin(), current_input_line_.end(),
                     [](char c) { return absl::ascii_iscntrl(c); }),
      current_input_line_.end());

  ResetLine();
  RestoreCurrentInputLine();
}

}  // namespace quic
