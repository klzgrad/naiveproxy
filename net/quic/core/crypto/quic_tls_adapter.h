// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_QUIC_TLS_ADAPTER_H_
#define NET_QUIC_CORE_CRYPTO_QUIC_TLS_ADAPTER_H_

#include "net/quic/core/crypto/crypto_message_parser.h"
#include "net/quic/core/quic_error_codes.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "third_party/boringssl/src/include/openssl/bio.h"

namespace net {

// QuicTlsAdapter provides an implementation of CryptoMessageParser that takes
// incoming messages and provides them to be read in a BIO (used by the TLS
// stack to read incoming messages). Messages written to the BIO by the TLS
// stack are provided to the QuicTlsAdapter's consumer through the
// OnDataReceived method of the consumer's implementation of
// QuicTlsAdapter::Visitor.
//
// QuicTlsAdapter also provides an implementation of the BIO interface,
// openssl's abstraction used by the TLS stack for I/O. The BIO interface
// provides BIO_read, BIO_write, and BIO_flush methods, with an API very similar
// to Berkeley sockets. This is a non-blocking interface - if data is not
// available for the BIO consumer to read with BIO_read, it returns 0 bytes of
// data, and the BIO consumer must handle waiting for more data and only calling
// BIO_read once data is available to read. With a QuicTlsAdapter, the signal
// that data is available to read is provided by
// QuicTlsAdapter::Visitor::OnDataAvailableForBIO.
//
// In effect, the QuicTlsAdapter moves messages between the QuicCryptoStream and
// the TLS stack. On one end, the QuicTlsAdapter implements CryptoMessageParser
// to take incoming messages and make them available to be read through the BIO,
// and on the other end, it takes messages written to the BIO and once the BIO
// flushes them, sends them out to the QuicStream via the
// QuicTlsAdapter::Visitor.
//
// Data flows from a QuicCryptoStream to the TLS stack like so:
//  1. QuicCryptoStream::OnDataAvailable is called (by QuicStream) when data is
//     available on the stream.
//  2. OnDataAvailable calls CryptoMessageParser::ProcessInput; in the case of a
//     TLS crypto stream, this is QuicTlsAdapter::ProcessInput.
//  3. ProcessInput saves the data to QuicTlsAdapter's read buffer, and signals
//     that data is available to read by calling
//     QuicTlsAdapter::Visitor::OnDataAvailableForBIO.
//  4. TlsHandshaker (which implements QuicTlsAdapter::Visitor) receives the
//     OnDataAvailableForBIO, and has the TLS stack continue its handshake.
//  5. The TLS stack calls BIO_read to read handshake messages, and this call is
//     made on a BIO backed by QuicTlsAdapter.
//  6. BIO_read calls QuicTlsAdapter::BIOReadWrapper, which calls
//     QuicTlsAdapter::Read (on the appropriate instance) which provides the
//     data from the read buffer written to by QuicTlsAdapter::ProcessInput.
//
// Data flows from the TLS stack to the QUIC crypto stream like so:
//  1. The TLS stack makes multiple calls to BIO_write as it generates handshake
//     messages. Via QuicTlsAdapter::BIOWriteWrapper and QuicTlsAdapter::Write,
//     this data gets appended to the QuicTlsAdapter's write buffer.
//  2. Once the TLS stack has written a flight of handshake messages, it calls
//     BIO_flush. This, via QuicTlsAdapter::BIOCtrlWrapper and
//     QuicTlsAdapter::Flush, signals to QuicTlsAdapter's Visitor that data has
//     been received.
//  3. QuicTlsAdapter::Flush calls
//     QuicTlsAdapter::Visitor::OnDataReceivedFromBIO with the contents of the
//     write buffer.
//  4. TlsHandshaker receives the data from OnDataReceivedFromBIO, and writes it
//     to the QUIC crypto stream.
class QUIC_EXPORT QuicTlsAdapter : public CryptoMessageParser {
 public:
  // QuicTlsAdapter::Visitor is notified whenever data is received (in either
  // direction). When data is read from the QUIC crypto stream,
  // Visitor::OnDataAvailableForBIO is called so that the Visitor can continue
  // reading from the BIO. (E.g. in the case of a TlsHandshaker as the Visitor,
  // it would continue the TLS handshake, which uses a BIO for I/O.) When data
  // is written to a QuicTlsAdapter's BIO interface and then flushed,
  // Visitor::OnDataReceivedFromBIO is called to provide the Visitor with the
  // data to write to the QUIC crypto stream.
  class QUIC_EXPORT Visitor {
   public:
    virtual ~Visitor() {}

    // OnDataAvailableForBIO is called when QuicTlsAdapter has received data
    // (via ProcessInput) that is now available to be read by the BIO.
    virtual void OnDataAvailableForBIO() = 0;

    // OnDataReceivedFromBIO is called when data is written to the BIO. For
    // example, when the TLS stack writes messages to the BIO and then flushes
    // them, the resulting data will be made available to the Visitor via this
    // method, so that the Visitor can write the messages to the QuicStream. The
    // stringpiece |data| is only valid during the execution of this function;
    // implementations must consume all of |data|.
    virtual void OnDataReceivedFromBIO(const QuicStringPiece& data) = 0;
  };

  // Constructs a QuicTlsAdapter that will notify |visitor| when data is
  // available in either direction. The provided Visitor must outlive the
  // QuicTlsAdapter.
  explicit QuicTlsAdapter(Visitor* visitor);

  ~QuicTlsAdapter() override;

  QuicErrorCode error() const override;
  const std::string& error_detail() const override;
  bool ProcessInput(QuicStringPiece input, Perspective perspective) override;
  size_t InputBytesRemaining() const override;

  BIO* bio() { return bio_.get(); }

 private:
  // The following methods, Read, Write, and Flush, are used to implement the
  // BIO.

  // Read copies up to |len| bytes from |read_buffer_| into |out|. It returns
  // the number of bytes copied, zero on EOF, or a negative number on error.
  int Read(char* out, int len);

  // Write appends |len| bytes from |in| to |write_buffer_|. It returns |len|,
  // or a negative number on error.
  int Write(const char* in, int len);

  // Flush calls Visitor::OnDataReceivedFromBIO with the data in
  // |write_buffer_| and then empties the buffer.
  void Flush();

  // Used by the static BIO*Wrapper methods to get the QuicTlsAdapter instance
  // to call Read/Write/Flush on.
  static QuicTlsAdapter* GetAdapter(BIO* bio);

  // Functions used to build a BIO_METHOD vtable. BIOReadWrapper calls Read,
  // BIOWriteWrapper calls Write, and BIOCtrlWrapper calls Flush if |cmd| is
  // |BIO_CTRL_FLUSH|.

  static int BIOReadWrapper(BIO* bio, char* out, int len);
  static int BIOWriteWrapper(BIO* bio, const char* in, int len);
  // BIOCtrlWrapper has the type it does so it can be used as the |ctrl| field
  // in a BIO_METHOD struct, hence the use of long as an argument  and return
  // type.
  // NOLINTNEXTLINE
  static long BIOCtrlWrapper(BIO* bio, int cmd, long larg, void* parg);

  static const BIO_METHOD kBIOMethod;

  // Visitor to call when QuicTlsAdapter receives data (in either direction).
  Visitor* visitor_;
  bssl::UniquePtr<BIO> bio_;

  // Buffer of data received from ProcessInput waiting to be read by the BIO.
  std::string read_buffer_;

  // Buffer of data received from the BIO waiting to be handed off to
  // Visitor::OnDataReceivedFromBIO.
  std::string write_buffer_;

  std::string error_detail_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_QUIC_TLS_ADAPTER_H_
