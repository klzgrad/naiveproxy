// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_KEY_VALUE_PAIR_H_
#define QUICHE_QUIC_MOQT_MOQT_KEY_VALUE_PAIR_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_data_writer.h"

namespace moqt {

// Encodes a list of key-value pairs common to both parameters and extensions.
// If the key is odd, it is a length-prefixed string (which may encode further
// item-specific structure). If the key is even, it is a varint.
// This class does not interpret the semantic meaning of the keys and values.
// Keys must be ordered.
class QUICHE_EXPORT KeyValuePairList {
 public:
  KeyValuePairList() = default;

  size_t size() const { return map_.size(); }
  void insert(uint64_t key, std::variant<uint64_t, absl::string_view> value);
  size_t count(uint64_t key) const { return map_.count(key); }
  bool contains(uint64_t key) const { return map_.contains(key); }

  using ValueCallback = quiche::UnretainedCallback<bool(
      uint64_t, std::variant<uint64_t, absl::string_view>)>;
  // Iterates through the whole list in increasing numerical order of key, and
  // executes |callback| for each element.
  // Returns false if |callback| returns false for any element.
  bool ForEach(ValueCallback callback) const;
  using ValueVector = std::vector<std::variant<uint64_t, absl::string_view>>;
  ValueVector Get(uint64_t key) const;
  void clear() { map_.clear(); }
  bool operator==(const KeyValuePairList& other) const = default;
  KeyValuePairList& operator=(const KeyValuePairList& other) = default;

 private:
  absl::btree_multimap<uint64_t, std::variant<uint64_t, std::string>> map_;
};

enum AuthTokenType : uint64_t {
  kOutOfBand = 0x0,

  kMaxAuthTokenType = 0x0,
};
enum AuthTokenAliasType : uint64_t {
  kDelete = 0x0,
  kRegister = 0x1,
  kUseAlias = 0x2,
  kUseValue = 0x3,

  kMaxValue = 0x3,
};
struct AuthToken {
  AuthToken(uint64_t alias, AuthTokenAliasType alias_type)
      : alias_type(alias_type), alias(alias) {
    QUICHE_DCHECK(alias_type == AuthTokenAliasType::kDelete ||
                  alias_type == AuthTokenAliasType::kUseAlias);
  }
  AuthToken(uint64_t alias, AuthTokenType type, absl::string_view value)
      : alias_type(AuthTokenAliasType::kRegister),
        alias(alias),
        type(type),
        value(value) {}
  AuthToken(AuthTokenType type, absl::string_view value)
      : alias_type(AuthTokenAliasType::kUseValue), type(type), value(value) {}
  bool operator==(const AuthToken& other) const = default;

  AuthTokenAliasType alias_type;
  std::optional<uint64_t> alias;
  std::optional<AuthTokenType> type;
  std::optional<std::string> value;
};

enum class QUICHE_EXPORT MoqtFilterType : uint64_t {
  kNextGroupStart = 0x1,
  kLargestObject = 0x2,
  kAbsoluteStart = 0x3,
  kAbsoluteRange = 0x4,
};
class QUICHE_EXPORT SubscriptionFilter {
 public:
  // Constructors prevent illegal constructions.
  explicit SubscriptionFilter(MoqtFilterType type) : type_(type) {
    QUICHE_DCHECK(type == MoqtFilterType::kNextGroupStart ||
                  type == MoqtFilterType::kLargestObject);
  }
  explicit SubscriptionFilter(Location start)
      : type_(MoqtFilterType::kAbsoluteStart), start_(start) {}
  SubscriptionFilter(Location start, uint64_t end_group)
      : type_(MoqtFilterType::kAbsoluteRange),
        start_(start),
        end_group_(end_group) {
    QUICHE_DCHECK(end_group >= start.group);
  }

  MoqtFilterType type() const { return type_; }
  Location start() const { return start_; }
  uint64_t end_group() const { return end_group_; }
  // If true, the filter does not depend on knowing LargestObject, or we
  // already know LargestObject. A Subscriber cannot have an unknown window,
  // because it can't process an object without getting the track alias from
  // the SubscribeOk.
  bool WindowKnown() const {
    return type_ == MoqtFilterType::kAbsoluteStart ||
           type_ == MoqtFilterType::kAbsoluteRange;
  }
  // Publishers should not call InWindow() if !WindowKnown(), as they should
  // not forward objects without knowing the window.
  bool InWindow(Location location) const {
    return location >= start_ && location.group <= end_group_;
  }
  bool InWindow(uint64_t group) const {
    return group >= start_.group && group <= end_group_;
  }
  // Update the filter when LargestObject is learned. If the type is
  // LargestObject or NextGroupStart, changes the type to AbsoluteStart.
  void OnLargestObject(const std::optional<Location>& largest_object);
  SubscriptionFilter& operator=(const SubscriptionFilter& other) = default;
  bool operator==(const SubscriptionFilter& other) const = default;

 private:
  MoqtFilterType type_;
  // These could be std::optional, but it would just add to the class size.
  Location start_ = Location(0, 0);
  uint64_t end_group_ = kMaxGroupId;
};

// Setup parameters.
inline constexpr uint64_t kDefaultMaxRequestId = 0;
// TODO(martinduke): Implement an auth token cache.
inline constexpr uint64_t kDefaultMaxAuthTokenCacheSize = 0;
inline constexpr bool kDefaultSupportObjectAcks = false;
enum class QUICHE_EXPORT SetupParameter : uint64_t {
  kPath = 0x1,
  kMaxRequestId = 0x2,
  kAuthorizationToken = 0x3,
  kMaxAuthTokenCacheSize = 0x4,
  kAuthority = 0x5,
  kMoqtImplementation = 0x7,

  // QUICHE-specific extensions.
  // Indicates support for OACK messages.
  kSupportObjectAcks = 0xbbf1438,
};
// TODO(martinduke): Refactor this to be more like TrackExtensions.
struct QUICHE_EXPORT SetupParameters {
  SetupParameters() = default;
  // Constructors for tests.
  SetupParameters(absl::string_view path, absl::string_view authority,
                  uint64_t max_request_id)
      : path(path), max_request_id(max_request_id), authority(authority) {}
  SetupParameters(uint64_t max_request_id) : max_request_id(max_request_id) {}

  std::optional<std::string> path;
  std::optional<uint64_t> max_request_id;
  // TODO(martinduke): Turn authorization_token into structured data.
  std::vector<AuthToken> authorization_tokens;
  std::optional<uint64_t> max_auth_token_cache_size;
  std::optional<std::string> authority;
  std::optional<std::string> moqt_implementation;

  std::optional<bool> support_object_acks;
  bool operator==(const SetupParameters& other) const = default;
  // Defined in moqt_framer.cc.
  KeyValuePairList ToKeyValuePairList() const;
  // Defined in moqt_parser.cc.
  // If the class is not initialized with the default constructor, it is likely
  // to return an error if a non-default field duplicates what is in |list|.
  absl::Status FromKeyValuePairList(const KeyValuePairList& list);
};

enum class MessageParameter : uint64_t {
  kDeliveryTimeout = 0x02,
  kAuthorizationToken = 0x03,
  kExpires = 0x08,
  kLargestObject = 0x09,
  kForward = 0x10,
  kSubscriberPriority = 0x20,
  kSubscriptionFilter = 0x21,
  kGroupOrder = 0x22,
  kNewGroupRequest = 0x32,

  kOackWindowSize = 0xbbF1438,
};
constexpr quic::QuicTimeDelta kDefaultDeliveryTimeout =
    quic::QuicTimeDelta::Infinite();
constexpr quic::QuicTimeDelta kDefaultExpires = quic::QuicTimeDelta::Infinite();
constexpr bool kDefaultForward = true;
constexpr uint8_t kDefaultSubscriberPriority = 128;
// TODO(martinduke): Refactor this to be more like TrackExtensions.
struct MessageParameters {
  MessageParameters() = default;
  MessageParameters(const MessageParameters&) = default;
  // Constructors for subscription filters with Auth tokens.
  MessageParameters(const MoqtFilterType& filter_type) {
    subscription_filter.emplace(filter_type);
  }
  MessageParameters(const Location& location) {
    subscription_filter.emplace(location);
  }

  // If |other| has a value in a particular field, replace the current value
  // with it. Otherwise, leave unchanged.
  void Update(const MessageParameters& other);

  std::optional<quic::QuicTimeDelta> delivery_timeout;
  std::vector<AuthToken> authorization_tokens;
  std::optional<quic::QuicTimeDelta> expires;
  std::optional<Location> largest_object;
  bool forward() const { return forward_.value_or(kDefaultForward); }
  void set_forward(bool forward) { forward_ = forward; }
  bool forward_has_value() const { return forward_.has_value(); }
  std::optional<MoqtPriority> subscriber_priority;
  std::optional<SubscriptionFilter> subscription_filter;
  std::optional<MoqtDeliveryOrder> group_order;
  std::optional<uint64_t> new_group_request;

  // QUICHE-specific parameters.
  std::optional<quic::QuicTimeDelta> oack_window_size;
  bool operator==(const MessageParameters& other) const = default;

  // Defined in moqt_framer.cc.
  KeyValuePairList ToKeyValuePairList() const;
  // Defined in moqt_parser.cc.
  // If the class is not initialized with the default constructor, it is likely
  // to return an error if a non-default field duplicates what is in |list|.
  absl::Status FromKeyValuePairList(const KeyValuePairList& list);

 private:
  // "if (forward)" is bug-prone because it returns forward_.has_value(). Make
  // it private and use public accessors instead.
  std::optional<bool> forward_;
};

enum class ExtensionHeader : uint64_t {
  kDeliveryTimeout = 0x02,
  kMaxCacheDuration = 0x04,
  kImmutableExtensions = 0x0b,
  kDefaultPublisherPriority = 0x0e,
  kDefaultPublisherGroupOrder = 0x22,
  kDynamicGroups = 0x30,
  kPriorGroupIdGap = 0x3c,
  kPriorObjectIdGap = 0x3e,
};
inline constexpr quic::QuicTimeDelta kDefaultMaxCacheDuration =
    quic::QuicTimeDelta::Infinite();
inline constexpr bool kDefaultImmutableExtensions = false;
inline constexpr MoqtPriority kDefaultPublisherPriority = 128;
inline constexpr MoqtDeliveryOrder kDefaultGroupOrder =
    MoqtDeliveryOrder::kAscending;
inline constexpr bool kDefaultDynamicGroups = false;
class TrackExtensions : public KeyValuePairList {
 public:
  TrackExtensions() = default;
  TrackExtensions(const TrackExtensions&) = default;
  // Constructor for Original publishers to create their extensions.
  TrackExtensions(std::optional<quic::QuicTimeDelta> delivery_timeout,
                  std::optional<quic::QuicTimeDelta> max_cache_duration,
                  std::optional<MoqtPriority> publisher_priority,
                  std::optional<MoqtDeliveryOrder> group_order,
                  std::optional<bool> dynamic_groups,
                  std::optional<absl::string_view> immutable_extensions);

  // If present and well-formed, returns the value of the extension. Returns the
  // default value if missing or ill-formed.
  quic::QuicTimeDelta delivery_timeout() const;
  quic::QuicTimeDelta max_cache_duration() const;
  absl::string_view immutable_extensions() const;
  MoqtPriority default_publisher_priority() const;
  MoqtDeliveryOrder default_publisher_group_order() const;
  bool dynamic_groups() const;

  // Returns false if the extension list contains illegal values or illegally
  // duplicated extensions.
  bool Validate() const;
  bool operator==(const TrackExtensions& other) const = default;
  TrackExtensions& operator=(const TrackExtensions& other) = default;

 private:
  // Returns the value of the extension if there is exactly one, otherwise
  // returns std::nullopt. Must not be called on odd extension types.
  std::optional<uint64_t> GetValueIfExactlyOne(ExtensionHeader header) const;
  // Verifies that there is no more that one instance of an extension, and if
  // present, that the value is acceptable.
  bool ValidateInner(ExtensionHeader header, std::optional<uint64_t> min_value,
                     std::optional<uint64_t> max_value) const;
};

// TODO(martinduke): Extension Headers (MOQT draft-16 Sec 11)

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_KEY_VALUE_PAIR_H_
