// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_INTERACTIVE_CLI_H_
#define QUICHE_QUIC_TOOLS_INTERACTIVE_CLI_H_

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {

// InteractiveCli is a tool that lets the user type input while the program is
// outputting things into the terminal. Only works on Unix-like platforms.
class InteractiveCli : public QuicSocketEventListener {
 public:
  using LineCallback = quiche::MultiUseCallback<void(absl::string_view)>;

  // `event_loop` must outlive the object. `line_callback` is called whenever
  // user enters a line of text into the terminal.
  InteractiveCli(QuicEventLoop* event_loop, LineCallback line_callback);
  ~InteractiveCli();
  InteractiveCli(InteractiveCli&) = delete;
  InteractiveCli(InteractiveCli&&) = delete;
  InteractiveCli& operator=(InteractiveCli&) = delete;
  InteractiveCli& operator=(InteractiveCli&&) = delete;

  // Outputs a line of text into the terminal, and then restores the user input
  // prompt. Use this instead of std::cout and other I/O functions.  Will crash
  // if stdin or stdout is not a terminal.  Does not support any form of
  // terminal editing except for backspace.
  void PrintLine(absl::string_view line);

  // Process kSocketReadable events on STDIN.
  void OnSocketEvent(QuicEventLoop* event_loop, SocketFd fd,
                     QuicSocketEventMask events) override;

 private:
  // Clears the current line in the terminal.
  void ResetLine();
  // Prints the pending user input.
  void RestoreCurrentInputLine();

  QuicEventLoop* event_loop_;  // Not owned.
  LineCallback line_callback_;
  // Avoid including termios.h by storing the old termios as an array of bytes.
  std::unique_ptr<char[]> old_termios_ = nullptr;
  // Buffered user input.
  std::string current_input_line_;
  // Prompt printed before the user input line.
  std::string prompt_ = "> ";
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_INTERACTIVE_CLI_H_
