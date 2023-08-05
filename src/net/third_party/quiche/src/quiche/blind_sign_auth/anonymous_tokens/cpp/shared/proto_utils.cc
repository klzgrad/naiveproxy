// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "quiche/blind_sign_auth/anonymous_tokens/cpp/shared/proto_utils.h"

namespace private_membership {
namespace anonymous_tokens {

absl::StatusOr<AnonymousTokensUseCase> ParseUseCase(
    absl::string_view use_case) {
  AnonymousTokensUseCase parsed_use_case;
  if (!AnonymousTokensUseCase_Parse(std::string(use_case), &parsed_use_case) ||
      parsed_use_case == ANONYMOUS_TOKENS_USE_CASE_UNDEFINED) {
    return absl::InvalidArgumentError(
        "Invalid / undefined use case cannot be parsed.");
  }
  return parsed_use_case;
}

absl::StatusOr<absl::Time> TimeFromProto(
    const quiche::protobuf::Timestamp& proto) {
  const auto sec = proto.seconds();
  const auto ns = proto.nanos();
  // sec must be [0001-01-01T00:00:00Z, 9999-12-31T23:59:59.999999999Z]
  if (sec < -62135596800 || sec > 253402300799) {
    return absl::InvalidArgumentError(absl::StrCat("seconds=", sec));
  }
  if (ns < 0 || ns > 999999999) {
    return absl::InvalidArgumentError(absl::StrCat("nanos=", ns));
  }
  return absl::FromUnixSeconds(proto.seconds()) +
         absl::Nanoseconds(proto.nanos());
}

absl::StatusOr<quiche::protobuf::Timestamp> TimeToProto(absl::Time time) {
  quiche::protobuf::Timestamp proto;
  const int64_t seconds = absl::ToUnixSeconds(time);
  proto.set_seconds(seconds);
  proto.set_nanos((time - absl::FromUnixSeconds(seconds)) /
                  absl::Nanoseconds(1));
  // seconds must be [0001-01-01T00:00:00Z, 9999-12-31T23:59:59.999999999Z]
  if (seconds < -62135596800 || seconds > 253402300799) {
    return absl::InvalidArgumentError(absl::StrCat("seconds=", seconds));
  }
  const int64_t ns = proto.nanos();
  if (ns < 0 || ns > 999999999) {
    return absl::InvalidArgumentError(absl::StrCat("nanos=", ns));
  }
  return proto;
}

}  // namespace anonymous_tokens
}  // namespace private_membership
