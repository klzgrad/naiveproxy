// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// moqt_ingestion_server is a simple command-line utility that accepts incoming
// ANNOUNCE messages and records them into a file.

#include <sys/stat.h>

#include <cerrno>
#include <cstdint>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/node_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_track.h"
#include "quiche/quic/moqt/tools/moqt_server.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_default_proof_providers.h"
#include "quiche/common/platform/api/quiche_file_utils.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_ip_address.h"

// Utility code for working with directories.
// TODO: make those cross-platform and move into quiche_file_utils.h.
namespace {
absl::Status IsDirectory(absl::string_view path) {
  std::string directory(path);
  struct stat directory_stat;
  int result = ::stat(directory.c_str(), &directory_stat);
  if (result != 0) {
    return absl::ErrnoToStatus(errno, "Failed to stat the directory");
  }
  if (!S_ISDIR(directory_stat.st_mode)) {
    return absl::InvalidArgumentError("Requested path is not a directory");
  }
  return absl::OkStatus();
}

absl::Status MakeDirectory(absl::string_view path) {
  int result = ::mkdir(std::string(path).c_str(), 0755);
  if (result != 0) {
    return absl::ErrnoToStatus(errno, "Failed to create directory");
  }
  return absl::OkStatus();
}
}  // namespace

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, allow_invalid_track_namespaces, false,
    "If true, invalid track namespaces will be escaped rather than rejected.");
DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, tracks, "video,audio",
    "List of track names to request from the peer.");

namespace moqt {
namespace {

bool IsValidTrackNamespaceChar(char c) {
  // Since we using track namespaces for directory names, limit the set of
  // allowed characters.
  return absl::ascii_isalnum(c) || c == '-' || c == '_';
}

bool IsValidTrackNamespace(absl::string_view track_namespace) {
  return absl::c_all_of(track_namespace, IsValidTrackNamespaceChar);
}

std::string CleanUpTrackNamespace(absl::string_view track_namespace) {
  std::string output(track_namespace);
  for (char& c : output) {
    if (!IsValidTrackNamespaceChar(c)) {
      c = '_';
    }
  }
  return output;
}

// Maintains the state for individual incoming MoQT sessions.
class MoqtIngestionHandler {
 public:
  explicit MoqtIngestionHandler(MoqtSession* session,
                                absl::string_view output_root)
      : session_(session), output_root_(output_root) {
    session_->callbacks().incoming_announce_callback =
        absl::bind_front(&MoqtIngestionHandler::OnAnnounceReceived, this);
  }

  std::optional<MoqtAnnounceErrorReason> OnAnnounceReceived(
      absl::string_view track_namespace) {
    if (!IsValidTrackNamespace(track_namespace) &&
        !quiche::GetQuicheCommandLineFlag(
            FLAGS_allow_invalid_track_namespaces)) {
      QUICHE_DLOG(WARNING) << "Rejected remote announce as it contained "
                              "disallowed characters; namespace: "
                           << track_namespace;
      return MoqtAnnounceErrorReason{
          MoqtAnnounceErrorCode::kInternalError,
          "Track namespace contains disallowed characters"};
    }

    std::string directory_name = absl::StrCat(
        CleanUpTrackNamespace(track_namespace), "_",
        absl::FormatTime("%Y%m%d_%H%M%S", absl::Now(), absl::UTCTimeZone()));
    std::string directory_path = quiche::JoinPath(output_root_, directory_name);
    auto [it, added] = subscribed_namespaces_.emplace(
        track_namespace, NamespaceHandler(directory_path));
    if (!added) {
      // Received before; should be handled by already existing subscriptions.
      return std::nullopt;
    }

    if (absl::Status status = MakeDirectory(directory_path); !status.ok()) {
      subscribed_namespaces_.erase(it);
      QUICHE_LOG(ERROR) << "Failed to create directory " << directory_path
                        << "; " << status;
      return MoqtAnnounceErrorReason{MoqtAnnounceErrorCode::kInternalError,
                                     "Failed to create output directory"};
    }

    std::string track_list = quiche::GetQuicheCommandLineFlag(FLAGS_tracks);
    std::vector<absl::string_view> tracks_to_subscribe =
        absl::StrSplit(track_list, ',', absl::AllowEmpty());
    for (absl::string_view track : tracks_to_subscribe) {
      session_->SubscribeCurrentGroup(track_namespace, track, &it->second);
    }

    return std::nullopt;
  }

 private:
  class NamespaceHandler : public RemoteTrack::Visitor {
   public:
    explicit NamespaceHandler(absl::string_view directory)
        : directory_(directory) {}

    void OnReply(
        const FullTrackName& full_track_name,
        std::optional<absl::string_view> error_reason_phrase) override {
      if (error_reason_phrase.has_value()) {
        QUICHE_LOG(ERROR) << "Failed to subscribe to the peer track "
                          << full_track_name.track_namespace << " "
                          << full_track_name.track_name << ": "
                          << *error_reason_phrase;
      }
    }

    void OnObjectFragment(const FullTrackName& full_track_name,
                          uint64_t group_sequence, uint64_t object_sequence,
                          uint64_t /*object_send_order*/,
                          MoqtForwardingPreference /*forwarding_preference*/,
                          absl::string_view object,
                          bool /*end_of_message*/) override {
      std::string file_name = absl::StrCat(group_sequence, "-", object_sequence,
                                           ".", full_track_name.track_name);
      std::string file_path = quiche::JoinPath(directory_, file_name);
      std::ofstream output(file_path, std::ios::binary | std::ios::ate);
      output.write(object.data(), object.size());
      output.close();
    }

   private:
    std::string directory_;
  };

  MoqtSession* session_;  // Not owned.
  std::string output_root_;
  absl::node_hash_map<std::string, NamespaceHandler> subscribed_namespaces_;
};

absl::StatusOr<MoqtConfigureSessionCallback> IncomingSessionHandler(
    std::string output_root, absl::string_view path) {
  if (path != "/ingest") {
    return absl::NotFoundError("Unknown endpoint; try \"/ingest\".");
  }
  return [output_root](MoqtSession* session) {
    auto handler = std::make_unique<MoqtIngestionHandler>(session, output_root);
    session->callbacks().session_deleted_callback = [handler =
                                                         std::move(handler)] {};
  };
}

}  // namespace
}  // namespace moqt

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, bind_address, "127.0.0.1",
                                "Local IP address to bind to");
DEFINE_QUICHE_COMMAND_LINE_FLAG(uint16_t, port, 8000,
                                "Port for the server to listen on");

int main(int argc, char** argv) {
  const char* usage = "Usage: moqt_ingestion_server [options] output_directory";
  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (args.size() != 1) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }

  std::string output_directory = args[0];
  if (absl::Status stat_status = IsDirectory(output_directory);
      !stat_status.ok()) {
    if (absl::IsNotFound(stat_status)) {
      absl::Status mkdir_status = MakeDirectory(output_directory);
      if (!mkdir_status.ok()) {
        QUICHE_LOG(ERROR) << "Failed to create output directory: "
                          << mkdir_status;
        return 1;
      }
    } else {
      QUICHE_LOG(ERROR) << stat_status;
      return 1;
    }
  }

  moqt::MoqtServer server(
      quiche::CreateDefaultProofSource(),
      absl::bind_front(moqt::IncomingSessionHandler, output_directory));
  quiche::QuicheIpAddress bind_address;
  QUICHE_CHECK(bind_address.FromString(
      quiche::GetQuicheCommandLineFlag(FLAGS_bind_address)));
  server.quic_server().CreateUDPSocketAndListen(quic::QuicSocketAddress(
      bind_address, quiche::GetQuicheCommandLineFlag(FLAGS_port)));
  server.quic_server().HandleEventsForever();

  return 0;
}
