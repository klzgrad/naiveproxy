// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/moqt/tools/chat_client.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/tools/interactive_cli.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, output_file, "",
    "chat messages will stream to a file instead of stdout");

// Writes messages to a file, when directed from the command line.
class FileOutput : public moqt::ChatUserInterface {
 public:
  explicit FileOutput(absl::string_view filename, absl::string_view username)
      : username_(username) {
    output_file_.open(filename);
    output_file_ << "Chat transcript:\n";
    output_file_.flush();
    std::cout << "Fully connected. Messages are in the output file. Exit the "
              << "session by entering /exit\n";
  }

  ~FileOutput() override { output_file_.close(); }

  void Initialize(quic::InteractiveCli::LineCallback callback,
                  quic::QuicEventLoop* event_loop) override {
    callback_ = std::move(callback);
    event_loop_ = event_loop;
  }

  void WriteToOutput(absl::string_view user,
                     absl::string_view message) override {
    if (message.empty()) {
      return;
    }
    output_file_ << user << ": " << message << "\n\n";
    output_file_.flush();
  }

  void IoLoop() override {
    std::string message_to_send;
    QUIC_BUG_IF(quic_bug_moq_chat_user_interface_unitialized,
                event_loop_ == nullptr)
        << "IoLoop called before Initialize";
    while (poll(&poll_settings_, 1, 0) <= 0) {
      event_loop_->RunEventLoopOnce(moqt::kChatEventLoopDuration);
    }
    std::getline(std::cin, message_to_send);
    callback_(message_to_send);
    WriteToOutput(username_, message_to_send);
  }

 private:
  quic::QuicEventLoop* event_loop_;
  quic::InteractiveCli::LineCallback callback_;
  std::ofstream output_file_;
  absl::string_view username_;
  struct pollfd poll_settings_ = {
      0,
      POLLIN,
      POLLIN,
  };
};

// Writes messages to the terminal, without messing up entry of new messages.
class CliOutput : public moqt::ChatUserInterface {
 public:
  void Initialize(quic::InteractiveCli::LineCallback callback,
                  quic::QuicEventLoop* event_loop) override {
    cli_ =
        std::make_unique<quic::InteractiveCli>(event_loop, std::move(callback));
    event_loop_ = event_loop;
    cli_->PrintLine("Fully connected. Enter '/exit' to exit the chat.\n");
  }

  void WriteToOutput(absl::string_view user,
                     absl::string_view message) override {
    QUIC_BUG_IF(quic_bug_moq_chat_user_interface_unitialized, cli_ == nullptr)
        << "WriteToOutput called before Initialize";
    cli_->PrintLine(absl::StrCat(user, ": ", message));
  }

  void IoLoop() override {
    QUIC_BUG_IF(quic_bug_moq_chat_user_interface_unitialized,
                event_loop_ == nullptr)
        << "IoLoop called before Initialize";
    event_loop_->RunEventLoopOnce(moqt::kChatEventLoopDuration);
  }

 private:
  quic::QuicEventLoop* event_loop_;
  std::unique_ptr<quic::InteractiveCli> cli_;
};

// A client for MoQT over chat, used for interop testing. See
// https://afrind.github.io/draft-frindell-moq-chat/draft-frindell-moq-chat.html
int main(int argc, char* argv[]) {
  const char* usage = "Usage: chat_client [options] <url> <username> <chat-id>";
  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (args.size() != 3) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }
  quic::QuicUrl url(args[0], "https");
  quic::QuicServerId server_id(url.host(), url.port());
  std::string path = url.PathParamsQuery();
  const std::string& username = args[1];
  const std::string& chat_id = args[2];
  std::string output_filename =
      quiche::GetQuicheCommandLineFlag(FLAGS_output_file);
  std::unique_ptr<moqt::ChatUserInterface> interface;

  if (!output_filename.empty()) {
    interface = std::make_unique<FileOutput>(output_filename, username);
  } else {  // Use the CLI.
    interface = std::make_unique<CliOutput>();
  }
  moqt::ChatClient client(
      server_id,
      quiche::GetQuicheCommandLineFlag(FLAGS_disable_certificate_verification),
      std::move(interface));

  if (!client.Connect(path, username, chat_id)) {
    return 1;
  }
  if (!client.AnnounceAndSubscribe()) {
    return 1;
  }
  client.IoLoop();
  return 0;
}
