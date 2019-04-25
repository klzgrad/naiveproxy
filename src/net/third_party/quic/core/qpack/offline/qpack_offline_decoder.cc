// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/offline/qpack_offline_decoder.h"

#include <cstdint>
#include <utility>

#include "net/third_party/quic/core/qpack/qpack_test_utils.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_endian.h"
#include "net/third_party/quic/platform/api/quic_file_utils.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"

namespace quic {

QpackOfflineDecoder::QpackOfflineDecoder()
    : encoder_stream_error_detected_(false),
      decoder_(this, &decoder_stream_sender_delegate_) {}

bool QpackOfflineDecoder::DecodeAndVerifyOfflineData(
    QuicStringPiece input_filename,
    QuicStringPiece expected_headers_filename) {
  if (!ParseInputFilename(input_filename)) {
    QUIC_LOG(ERROR) << "Error parsing input filename " << input_filename;
    return false;
  }

  if (!DecodeHeaderBlocksFromFile(input_filename)) {
    QUIC_LOG(ERROR) << "Error decoding header blocks in " << input_filename;
    return false;
  }

  if (!VerifyDecodedHeaderLists(expected_headers_filename)) {
    QUIC_LOG(ERROR) << "Header lists decoded from " << input_filename
                    << " to not match expected headers parsed from "
                    << expected_headers_filename;
    return false;
  }

  return true;
}

void QpackOfflineDecoder::OnEncoderStreamError(QuicStringPiece error_message) {
  QUIC_LOG(ERROR) << "Encoder stream error: " << error_message;
  encoder_stream_error_detected_ = true;
}

bool QpackOfflineDecoder::ParseInputFilename(QuicStringPiece input_filename) {
  auto pieces = QuicTextUtils::Split(input_filename, '.');

  if (pieces.size() < 3) {
    QUIC_LOG(ERROR) << "Not enough fields in input filename " << input_filename;
    return false;
  }

  auto piece_it = pieces.rbegin();

  // Acknowledgement mode: 1 for immediate, 0 for none.
  bool immediate_acknowledgement = false;
  if (*piece_it == "0") {
    immediate_acknowledgement = false;
  } else if (*piece_it == "1") {
    immediate_acknowledgement = true;
  } else {
    QUIC_LOG(ERROR)
        << "Header acknowledgement field must be 0 or 1 in input filename "
        << input_filename;
    return false;
  }

  ++piece_it;

  // Maximum allowed number of blocked streams.
  uint64_t max_blocked_streams = 0;
  if (!QuicTextUtils::StringToUint64(*piece_it, &max_blocked_streams)) {
    QUIC_LOG(ERROR) << "Error parsing part of input filename \"" << *piece_it
                    << "\" as an integer.";
    return false;
  }

  if (max_blocked_streams > 0) {
    // TODO(bnc): Implement blocked streams.
    QUIC_LOG(ERROR) << "Blocked streams not implemented.";
    return false;
  }

  ++piece_it;

  // Dynamic Table Size in bytes
  uint64_t dynamic_table_size = 0;
  if (!QuicTextUtils::StringToUint64(*piece_it, &dynamic_table_size)) {
    QUIC_LOG(ERROR) << "Error parsing part of input filename \"" << *piece_it
                    << "\" as an integer.";
    return false;
  }

  decoder_.SetMaximumDynamicTableCapacity(dynamic_table_size);

  return true;
}

bool QpackOfflineDecoder::DecodeHeaderBlocksFromFile(
    QuicStringPiece input_filename) {
  // Store data in |input_data_storage|; use a QuicStringPiece to efficiently
  // keep track of remaining portion yet to be decoded.
  QuicString input_data_storage;
  ReadFileContents(input_filename, &input_data_storage);
  QuicStringPiece input_data(input_data_storage);

  while (!input_data.empty()) {
    if (input_data.size() < sizeof(uint64_t) + sizeof(uint32_t)) {
      QUIC_LOG(ERROR) << "Unexpected end of input file.";
      return false;
    }

    uint64_t stream_id = QuicEndian::NetToHost64(
        *reinterpret_cast<const uint64_t*>(input_data.data()));
    input_data = input_data.substr(sizeof(uint64_t));

    uint32_t length = QuicEndian::NetToHost32(
        *reinterpret_cast<const uint32_t*>(input_data.data()));
    input_data = input_data.substr(sizeof(uint32_t));

    if (input_data.size() < length) {
      QUIC_LOG(ERROR) << "Unexpected end of input file.";
      return false;
    }

    QuicStringPiece data = input_data.substr(0, length);
    input_data = input_data.substr(length);

    if (stream_id == 0) {
      decoder_.DecodeEncoderStreamData(data);

      if (encoder_stream_error_detected_) {
        QUIC_LOG(ERROR) << "Error detected on encoder stream.";
        return false;
      }

      continue;
    }

    test::TestHeadersHandler headers_handler;

    auto progressive_decoder =
        decoder_.DecodeHeaderBlock(stream_id, &headers_handler);
    progressive_decoder->Decode(data);
    progressive_decoder->EndHeaderBlock();

    if (headers_handler.decoding_error_detected()) {
      QUIC_LOG(ERROR) << "Decoding error on stream " << stream_id;
      return false;
    }

    decoded_header_lists_.push_back(headers_handler.ReleaseHeaderList());
  }

  return true;
}

bool QpackOfflineDecoder::VerifyDecodedHeaderLists(
    QuicStringPiece expected_headers_filename) {
  // Store data in |expected_headers_data_storage|; use a QuicStringPiece to
  // efficiently keep track of remaining portion yet to be decoded.
  QuicString expected_headers_data_storage;
  ReadFileContents(expected_headers_filename, &expected_headers_data_storage);
  QuicStringPiece expected_headers_data(expected_headers_data_storage);

  while (!decoded_header_lists_.empty()) {
    spdy::SpdyHeaderBlock decoded_header_list =
        std::move(decoded_header_lists_.front());
    decoded_header_lists_.pop_front();

    spdy::SpdyHeaderBlock expected_header_list;
    if (!ReadNextExpectedHeaderList(&expected_headers_data,
                                    &expected_header_list)) {
      QUIC_LOG(ERROR)
          << "Error parsing expected header list to match next decoded "
             "header list.";
      return false;
    }

    if (!CompareHeaderBlocks(std::move(decoded_header_list),
                             std::move(expected_header_list))) {
      QUIC_LOG(ERROR) << "Decoded header does not match expected header.";
      return false;
    }
  }

  if (!expected_headers_data.empty()) {
    QUIC_LOG(ERROR)
        << "Not enough encoded header lists to match expected ones.";
    return false;
  }

  return true;
}

bool QpackOfflineDecoder::ReadNextExpectedHeaderList(
    QuicStringPiece* expected_headers_data,
    spdy::SpdyHeaderBlock* expected_header_list) {
  while (true) {
    QuicStringPiece::size_type endline = expected_headers_data->find('\n');

    // Even last header list must be followed by an empty line.
    if (endline == QuicStringPiece::npos) {
      QUIC_LOG(ERROR) << "Unexpected end of expected header list file.";
      return false;
    }

    if (endline == 0) {
      // Empty line indicates end of header list.
      *expected_headers_data = expected_headers_data->substr(1);
      return true;
    }

    QuicStringPiece header_field = expected_headers_data->substr(0, endline);
    auto pieces = QuicTextUtils::Split(header_field, '\t');

    if (pieces.size() != 2) {
      QUIC_LOG(ERROR) << "Header key and value must be separated by TAB.";
      return false;
    }

    expected_header_list->AppendValueOrAddHeader(pieces[0], pieces[1]);

    *expected_headers_data = expected_headers_data->substr(endline + 1);
  }
}

bool QpackOfflineDecoder::CompareHeaderBlocks(
    spdy::SpdyHeaderBlock decoded_header_list,
    spdy::SpdyHeaderBlock expected_header_list) {
  if (decoded_header_list == expected_header_list) {
    return true;
  }

  // The h2o decoder reshuffles the "content-length" header and pseudo-headers,
  // see
  // https://github.com/qpackers/qifs/blob/master/encoded/qpack-03/h2o/README.md.
  // Remove such headers one by one if they match.
  const char* kContentLength = "content-length";
  const char* kPseudoHeaderPrefix = ":";
  for (spdy::SpdyHeaderBlock::iterator decoded_it = decoded_header_list.begin();
       decoded_it != decoded_header_list.end();) {
    const QuicStringPiece key = decoded_it->first;
    if (key != kContentLength &&
        !QuicTextUtils::StartsWith(key, kPseudoHeaderPrefix)) {
      ++decoded_it;
      continue;
    }
    spdy::SpdyHeaderBlock::iterator expected_it =
        expected_header_list.find(key);
    if (expected_it == expected_header_list.end() ||
        decoded_it->second != expected_it->second) {
      ++decoded_it;
      continue;
    }
    // SpdyHeaderBlock does not support erasing by iterator, only by key.
    ++decoded_it;
    expected_header_list.erase(key);
    // This will invalidate |key|.
    decoded_header_list.erase(key);
  }

  return decoded_header_list == expected_header_list;
}

}  // namespace quic
