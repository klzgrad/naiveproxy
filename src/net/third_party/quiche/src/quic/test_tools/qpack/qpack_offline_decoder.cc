// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Decoder to test QPACK Offline Interop corpus
//
// See https://github.com/quicwg/base-drafts/wiki/QPACK-Offline-Interop for
// description of test data format.
//
// Example usage
//
//  cd $TEST_DATA
//  git clone https://github.com/qpackers/qifs.git
//  TEST_ENCODED_DATA=$TEST_DATA/qifs/encoded/qpack-06
//  TEST_QIF_DATA=$TEST_DATA/qifs/qifs
//  $BIN/qpack_offline_decoder \
//      $TEST_ENCODED_DATA/f5/fb-req.qifencoded.4096.100.0 \
//      $TEST_QIF_DATA/fb-req.qif
//      $TEST_ENCODED_DATA/h2o/fb-req-hq.out.512.0.1 \
//      $TEST_QIF_DATA/fb-req-hq.qif
//      $TEST_ENCODED_DATA/ls-qpack/fb-resp-hq.out.0.0.0 \
//      $TEST_QIF_DATA/fb-resp-hq.qif
//      $TEST_ENCODED_DATA/proxygen/netbsd.qif.proxygen.out.4096.0.0 \
//      $TEST_QIF_DATA/netbsd.qif
//

#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_offline_decoder.h"

#include <cstdint>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_file_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {

QpackOfflineDecoder::QpackOfflineDecoder()
    : encoder_stream_error_detected_(false) {}

bool QpackOfflineDecoder::DecodeAndVerifyOfflineData(
    quiche::QuicheStringPiece input_filename,
    quiche::QuicheStringPiece expected_headers_filename) {
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

void QpackOfflineDecoder::OnEncoderStreamError(
    quiche::QuicheStringPiece error_message) {
  QUIC_LOG(ERROR) << "Encoder stream error: " << error_message;
  encoder_stream_error_detected_ = true;
}

bool QpackOfflineDecoder::ParseInputFilename(
    quiche::QuicheStringPiece input_filename) {
  auto pieces = quiche::QuicheTextUtils::Split(input_filename, '.');

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
  if (!quiche::QuicheTextUtils::StringToUint64(*piece_it,
                                               &max_blocked_streams)) {
    QUIC_LOG(ERROR) << "Error parsing part of input filename \"" << *piece_it
                    << "\" as an integer.";
    return false;
  }

  ++piece_it;

  // Maximum Dynamic Table Capacity in bytes
  uint64_t maximum_dynamic_table_capacity = 0;
  if (!quiche::QuicheTextUtils::StringToUint64(
          *piece_it, &maximum_dynamic_table_capacity)) {
    QUIC_LOG(ERROR) << "Error parsing part of input filename \"" << *piece_it
                    << "\" as an integer.";
    return false;
  }
  qpack_decoder_ = std::make_unique<QpackDecoder>(
      maximum_dynamic_table_capacity, max_blocked_streams, this);
  qpack_decoder_->set_qpack_stream_sender_delegate(
      &decoder_stream_sender_delegate_);

  // The initial dynamic table capacity is zero according to
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#eviction.
  // However, for historical reasons, offline interop encoders use
  // |maximum_dynamic_table_capacity| as initial capacity.
  qpack_decoder_->OnSetDynamicTableCapacity(maximum_dynamic_table_capacity);

  return true;
}

bool QpackOfflineDecoder::DecodeHeaderBlocksFromFile(
    quiche::QuicheStringPiece input_filename) {
  // Store data in |input_data_storage|; use a quiche::QuicheStringPiece to
  // efficiently keep track of remaining portion yet to be decoded.
  std::string input_data_storage;
  ReadFileContents(input_filename, &input_data_storage);
  quiche::QuicheStringPiece input_data(input_data_storage);

  while (!input_data.empty()) {
    // Parse stream_id and length.
    if (input_data.size() < sizeof(uint64_t) + sizeof(uint32_t)) {
      QUIC_LOG(ERROR) << "Unexpected end of input file.";
      return false;
    }

    uint64_t stream_id = quiche::QuicheEndian::NetToHost64(
        *reinterpret_cast<const uint64_t*>(input_data.data()));
    input_data = input_data.substr(sizeof(uint64_t));

    uint32_t length = quiche::QuicheEndian::NetToHost32(
        *reinterpret_cast<const uint32_t*>(input_data.data()));
    input_data = input_data.substr(sizeof(uint32_t));

    if (input_data.size() < length) {
      QUIC_LOG(ERROR) << "Unexpected end of input file.";
      return false;
    }

    // Parse data.
    quiche::QuicheStringPiece data = input_data.substr(0, length);
    input_data = input_data.substr(length);

    // Process data.
    if (stream_id == 0) {
      qpack_decoder_->encoder_stream_receiver()->Decode(data);

      if (encoder_stream_error_detected_) {
        QUIC_LOG(ERROR) << "Error detected on encoder stream.";
        return false;
      }
    } else {
      auto headers_handler = std::make_unique<test::TestHeadersHandler>();
      auto progressive_decoder = qpack_decoder_->CreateProgressiveDecoder(
          stream_id, headers_handler.get());

      progressive_decoder->Decode(data);
      progressive_decoder->EndHeaderBlock();

      if (headers_handler->decoding_error_detected()) {
        QUIC_LOG(ERROR) << "Sync decoding error on stream " << stream_id << ": "
                        << headers_handler->error_message();
        return false;
      }

      decoders_.push_back({std::move(headers_handler),
                           std::move(progressive_decoder), stream_id});
    }

    // Move decoded header lists from TestHeadersHandlers and append them to
    // |decoded_header_lists_| while preserving the order in |decoders_|.
    while (!decoders_.empty() &&
           decoders_.front().headers_handler->decoding_completed()) {
      Decoder* decoder = &decoders_.front();

      if (decoder->headers_handler->decoding_error_detected()) {
        QUIC_LOG(ERROR) << "Async decoding error on stream "
                        << decoder->stream_id << ": "
                        << decoder->headers_handler->error_message();
        return false;
      }

      if (!decoder->headers_handler->decoding_completed()) {
        QUIC_LOG(ERROR) << "Decoding incomplete after reading entire"
                           " file, on stream "
                        << decoder->stream_id;
        return false;
      }

      decoded_header_lists_.push_back(
          decoder->headers_handler->ReleaseHeaderList());
      decoders_.pop_front();
    }
  }

  if (!decoders_.empty()) {
    DCHECK(!decoders_.front().headers_handler->decoding_completed());

    QUIC_LOG(ERROR) << "Blocked decoding uncomplete after reading entire"
                       " file, on stream "
                    << decoders_.front().stream_id;
    return false;
  }

  return true;
}

bool QpackOfflineDecoder::VerifyDecodedHeaderLists(
    quiche::QuicheStringPiece expected_headers_filename) {
  // Store data in |expected_headers_data_storage|; use a
  // quiche::QuicheStringPiece to efficiently keep track of remaining portion
  // yet to be decoded.
  std::string expected_headers_data_storage;
  ReadFileContents(expected_headers_filename, &expected_headers_data_storage);
  quiche::QuicheStringPiece expected_headers_data(
      expected_headers_data_storage);

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
    quiche::QuicheStringPiece* expected_headers_data,
    spdy::SpdyHeaderBlock* expected_header_list) {
  while (true) {
    quiche::QuicheStringPiece::size_type endline =
        expected_headers_data->find('\n');

    // Even last header list must be followed by an empty line.
    if (endline == quiche::QuicheStringPiece::npos) {
      QUIC_LOG(ERROR) << "Unexpected end of expected header list file.";
      return false;
    }

    if (endline == 0) {
      // Empty line indicates end of header list.
      *expected_headers_data = expected_headers_data->substr(1);
      return true;
    }

    quiche::QuicheStringPiece header_field =
        expected_headers_data->substr(0, endline);
    auto pieces = quiche::QuicheTextUtils::Split(header_field, '\t');

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
    const quiche::QuicheStringPiece key = decoded_it->first;
    if (key != kContentLength &&
        !quiche::QuicheTextUtils::StartsWith(key, kPseudoHeaderPrefix)) {
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
