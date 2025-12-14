/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/protozero/filtering/message_filter.h"

#include "perfetto/base/logging.h"
#include "perfetto/protozero/proto_utils.h"
#include "src/protozero/filtering/string_filter.h"

namespace protozero {

namespace {

// Inline helpers to append proto fields in output. They are the equivalent of
// the protozero::Message::AppendXXX() fields but don't require building and
// maintaining a full protozero::Message object or dealing with scattered
// output slices.
// All these functions assume there is enough space in the output buffer, which
// should be always the case assuming that we don't end up generating more
// output than input.

inline void AppendVarInt(uint32_t field_id, uint64_t value, uint8_t** out) {
  *out = proto_utils::WriteVarInt(proto_utils::MakeTagVarInt(field_id), *out);
  *out = proto_utils::WriteVarInt(value, *out);
}

// For fixed32 / fixed64.
template <typename INT_T /* uint32_t | uint64_t*/>
inline void AppendFixed(uint32_t field_id, INT_T value, uint8_t** out) {
  *out = proto_utils::WriteVarInt(proto_utils::MakeTagFixed<INT_T>(field_id),
                                  *out);
  memcpy(*out, &value, sizeof(value));
  *out += sizeof(value);
}

// For length-delimited (string, bytes) fields. Note: this function appends only
// the proto preamble and the varint field that states the length of the payload
// not the payload itself.
// In the case of submessages, the caller needs to re-write the length at the
// end in the in the returned memory area.
// The problem here is that, because of filtering, the length of a submessage
// might be < original length (the original length is still an upper-bound).
// Returns a pair with: (1) the pointer where the final length should be written
// into, (2) the length of the size field.
// The caller must write a redundant varint to match the original size (i.e.
// needs to use WriteRedundantVarInt()).
inline std::pair<uint8_t*, uint32_t> AppendLenDelim(uint32_t field_id,
                                                    uint32_t len,
                                                    uint8_t** out) {
  *out = proto_utils::WriteVarInt(proto_utils::MakeTagLengthDelimited(field_id),
                                  *out);
  uint8_t* size_field_start = *out;
  *out = proto_utils::WriteVarInt(len, *out);
  const size_t size_field_len = static_cast<size_t>(*out - size_field_start);
  return std::make_pair(size_field_start, size_field_len);
}
}  // namespace

MessageFilter::MessageFilter(Config config) : config_(std::move(config)) {
  // Push a state on the stack for the implicit root message.
  stack_.emplace_back();
}

MessageFilter::MessageFilter() : MessageFilter(Config()) {}

MessageFilter::~MessageFilter() = default;

bool MessageFilter::Config::LoadFilterBytecode(const void* filter_data,
                                               size_t len) {
  return filter_.Load(filter_data, len);
}

bool MessageFilter::Config::SetFilterRoot(
    std::initializer_list<uint32_t> field_ids) {
  uint32_t root_msg_idx = 0;
  for (uint32_t field_id : field_ids) {
    auto res = filter_.Query(root_msg_idx, field_id);
    if (!res.allowed || !res.nested_msg_field())
      return false;
    root_msg_idx = res.nested_msg_index;
  }
  root_msg_index_ = root_msg_idx;
  return true;
}

MessageFilter::FilteredMessage MessageFilter::FilterMessageFragments(
    const InputSlice* slices,
    size_t num_slices) {
  // First compute the upper bound for the output. The filtered message cannot
  // be > the original message.
  uint32_t total_len = 0;
  for (size_t i = 0; i < num_slices; ++i)
    total_len += slices[i].len;
  out_buf_.reset(new uint8_t[total_len]);
  out_ = out_buf_.get();
  out_end_ = out_ + total_len;

  // Reset the parser state.
  tokenizer_ = MessageTokenizer();
  error_ = false;
  stack_.clear();
  stack_.resize(2);
  // stack_[0] is a sentinel and should never be hit in nominal cases. If we
  // end up there we will just keep consuming the input stream and detecting
  // at the end, without hurting the fastpath.
  stack_[0].in_bytes_limit = UINT32_MAX;
  stack_[0].eat_next_bytes = UINT32_MAX;
  // stack_[1] is the actual root message.
  stack_[1].in_bytes_limit = total_len;
  stack_[1].msg_index = config_.root_msg_index();

  // Process the input data and write the output.
  for (size_t slice_idx = 0; slice_idx < num_slices; ++slice_idx) {
    const InputSlice& slice = slices[slice_idx];
    const uint8_t* data = static_cast<const uint8_t*>(slice.data);
    for (size_t i = 0; i < slice.len; ++i)
      FilterOneByte(data[i]);
  }

  // Construct the output object.
  PERFETTO_CHECK(out_ >= out_buf_.get() && out_ <= out_end_);
  auto used_size = static_cast<size_t>(out_ - out_buf_.get());
  FilteredMessage res{std::move(out_buf_), used_size};
  res.error = error_;
  if (stack_.size() != 1 || !tokenizer_.idle() ||
      stack_[0].in_bytes != total_len) {
    res.error = true;
  }
  return res;
}

void MessageFilter::FilterOneByte(uint8_t octet) {
  PERFETTO_DCHECK(!stack_.empty());

  auto* state = &stack_.back();
  StackState next_state{};
  bool push_next_state = false;

  if (state->eat_next_bytes > 0) {
    // This is the case where the previous tokenizer_.Push() call returned a
    // length delimited message which is NOT a submessage (a string or a bytes
    // field). We just want to consume it, and pass it through/filter strings
    // if the field was allowed.
    --state->eat_next_bytes;
    if (state->action == StackState::kPassthrough) {
      *(out_++) = octet;
    } else if (state->action == StackState::kFilterString) {
      *(out_++) = octet;
      if (state->eat_next_bytes == 0) {
        config_.string_filter().MaybeFilter(
            reinterpret_cast<char*>(state->filter_string_ptr),
            static_cast<size_t>(out_ - state->filter_string_ptr));
      }
    }
  } else {
    MessageTokenizer::Token token = tokenizer_.Push(octet);
    // |token| will not be valid() in most cases and this is WAI. When pushing
    // a varint field, only the last byte yields a token, all the other bytes
    // return an invalid token, they just update the internal tokenizer state.
    if (token.valid()) {
      auto filter = config_.filter().Query(state->msg_index, token.field_id);
      switch (token.type) {
        case proto_utils::ProtoWireType::kVarInt:
          if (filter.allowed && filter.simple_field())
            AppendVarInt(token.field_id, token.value, &out_);
          break;
        case proto_utils::ProtoWireType::kFixed32:
          if (filter.allowed && filter.simple_field())
            AppendFixed(token.field_id, static_cast<uint32_t>(token.value),
                        &out_);
          break;
        case proto_utils::ProtoWireType::kFixed64:
          if (filter.allowed && filter.simple_field())
            AppendFixed(token.field_id, static_cast<uint64_t>(token.value),
                        &out_);
          break;
        case proto_utils::ProtoWireType::kLengthDelimited:
          // Here we have two cases:
          // A. A simple string/bytes field: we just want to consume the next
          //    bytes (the string payload), optionally passing them through in
          //    output if the field is allowed.
          // B. This is a nested submessage. In this case we want to recurse and
          //    push a new state on the stack.
          // Note that we can't tell the difference between a
          // "non-allowed string" and a "non-allowed submessage". But it doesn't
          // matter because in both cases we just want to skip the next N bytes.
          const auto submessage_len = static_cast<uint32_t>(token.value);
          auto in_bytes_left = state->in_bytes_limit - state->in_bytes - 1;
          if (PERFETTO_UNLIKELY(submessage_len > in_bytes_left)) {
            // This is a malicious / malformed string/bytes/submessage that
            // claims to be larger than the outer message that contains it.
            return SetUnrecoverableErrorState();
          }

          if (filter.allowed && filter.nested_msg_field() &&
              submessage_len > 0) {
            // submessage_len == 0 is the edge case of a message with a 0-len
            // (but present) submessage. In this case, if allowed, we don't want
            // to push any further state (doing so would desync the FSM) but we
            // still want to emit it.
            // At this point |submessage_len| is only an upper bound. The
            // final message written in output can be <= the one in input,
            // only some of its fields might be allowed (also remember that
            // this class implicitly removes redundancy varint encoding of
            // len-delimited field lengths). The final length varint (the
            // return value of AppendLenDelim()) will be filled when popping
            // from |stack_|.
            auto size_field =
                AppendLenDelim(token.field_id, submessage_len, &out_);
            push_next_state = true;
            next_state.field_id = token.field_id;
            next_state.msg_index = filter.nested_msg_index;
            next_state.in_bytes_limit = submessage_len;
            next_state.size_field = size_field.first;
            next_state.size_field_len = size_field.second;
            next_state.out_bytes_written_at_start = out_written();
          } else {
            // A string or bytes field, or a 0 length submessage.
            state->eat_next_bytes = submessage_len;
            if (filter.allowed && filter.filter_string_field()) {
              state->action = StackState::kFilterString;
              AppendLenDelim(token.field_id, submessage_len, &out_);
              state->filter_string_ptr = out_;
            } else if (filter.allowed) {
              state->action = StackState::kPassthrough;
              AppendLenDelim(token.field_id, submessage_len, &out_);
            } else {
              state->action = StackState::kDrop;
            }
          }
          break;
      }  // switch(type)

      if (PERFETTO_UNLIKELY(track_field_usage_)) {
        IncrementCurrentFieldUsage(token.field_id, filter.allowed);
      }
    }  // if (token.valid)
  }  // if (eat_next_bytes == 0)

  ++state->in_bytes;
  while (state->in_bytes >= state->in_bytes_limit) {
    PERFETTO_DCHECK(state->in_bytes == state->in_bytes_limit);
    push_next_state = false;

    // We can't possibly write more than we read.
    const uint32_t msg_bytes_written = static_cast<uint32_t>(
        out_written() - state->out_bytes_written_at_start);
    PERFETTO_DCHECK(msg_bytes_written <= state->in_bytes_limit);

    // Backfill the length field of the
    proto_utils::WriteRedundantVarInt(msg_bytes_written, state->size_field,
                                      state->size_field_len);

    const uint32_t in_bytes_processes_for_last_msg = state->in_bytes;
    stack_.pop_back();
    PERFETTO_CHECK(!stack_.empty());
    state = &stack_.back();
    state->in_bytes += in_bytes_processes_for_last_msg;
    if (PERFETTO_UNLIKELY(!tokenizer_.idle())) {
      // If we hit this case, it means that we got to the end of a submessage
      // while decoding a field. We can't recover from this and we don't want to
      // propagate a broken sub-message.
      return SetUnrecoverableErrorState();
    }
  }

  if (push_next_state) {
    PERFETTO_DCHECK(tokenizer_.idle());
    stack_.emplace_back(std::move(next_state));
    state = &stack_.back();
  }
}

void MessageFilter::SetUnrecoverableErrorState() {
  error_ = true;
  stack_.clear();
  stack_.resize(1);
  auto& state = stack_[0];
  state.eat_next_bytes = UINT32_MAX;
  state.in_bytes_limit = UINT32_MAX;
  state.action = StackState::kDrop;
  out_ = out_buf_.get();  // Reset the write pointer.
}

void MessageFilter::IncrementCurrentFieldUsage(uint32_t field_id,
                                               bool allowed) {
  // Slowpath. Used mainly in offline tools and tests to workout used fields in
  // a proto.
  PERFETTO_DCHECK(track_field_usage_);

  // Field path contains a concatenation of varints, one for each nesting level.
  // e.g. y in message Root { Sub x = 2; }; message Sub { SubSub y = 7; }
  // is encoded as [varint(2) + varint(7)].
  // We use varint to take the most out of SSO (small string opt). In most cases
  // the path will fit in the on-stack 22 bytes, requiring no heap.
  std::string field_path;

  auto append_field_id = [&field_path](uint32_t id) {
    uint8_t buf[10];
    uint8_t* end = proto_utils::WriteVarInt(id, buf);
    field_path.append(reinterpret_cast<char*>(buf),
                      static_cast<size_t>(end - buf));
  };

  // Append all the ancestors IDs from the state stack.
  // The first entry of the stack has always ID 0 and we skip it (we don't know
  // the ID of the root message itself).
  PERFETTO_DCHECK(stack_.size() >= 2 && stack_[1].field_id == 0);
  for (size_t i = 2; i < stack_.size(); ++i)
    append_field_id(stack_[i].field_id);
  // Append the id of the field in the current message.
  append_field_id(field_id);
  field_usage_[field_path] += allowed ? 1 : -1;
}

}  // namespace protozero
