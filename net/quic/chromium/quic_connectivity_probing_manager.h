// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_QUIC_CONNECTIVITY_PROBING_MANAGER_H_
#define NET_QUIC_CHROMIUM_QUIC_CONNECTIVITY_PROBING_MANAGER_H_

#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/log/net_log_with_source.h"
#include "net/quic/chromium/quic_chromium_packet_reader.h"
#include "net/quic/chromium/quic_chromium_packet_writer.h"
#include "net/quic/core/quic_time.h"

namespace net {

// Responsible for sending and retransmitting connectivity probing packet on a
// designated path to the specified peer, and for notifying associated session
// when connectivity probe fails or succeeds.
class NET_EXPORT_PRIVATE QuicConnectivityProbingManager
    : public QuicChromiumPacketWriter::Delegate {
 public:
  // Delegate interface which receives notifications on network probing
  // results.
  class NET_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() {}
    // Called when probing on |network| succeeded. Caller hands off ownership
    // of |socket|, |writer| and |reader| for |network| to delegate.
    virtual void OnProbeNetworkSucceeded(
        NetworkChangeNotifier::NetworkHandle network,
        const QuicSocketAddress& self_address,
        std::unique_ptr<DatagramClientSocket> socket,
        std::unique_ptr<QuicChromiumPacketWriter> writer,
        std::unique_ptr<QuicChromiumPacketReader> reader) = 0;

    // Called when probing on |network| fails.
    virtual void OnProbeNetworkFailed(
        NetworkChangeNotifier::NetworkHandle network) = 0;

    // Called when a connectivity probing packet needs to be sent to
    // |peer_address| using |writer|. Returns true if subsequent packets can be
    // written by the |writer|.
    virtual bool OnSendConnectivityProbingPacket(
        QuicChromiumPacketWriter* writer,
        const QuicSocketAddress& peer_address) = 0;
  };

  QuicConnectivityProbingManager(Delegate* delegate,
                                 base::SequencedTaskRunner* task_runner);
  ~QuicConnectivityProbingManager();

  // QuicChromiumPacketWriter::Delegate interface.
  int HandleWriteError(int error_code,
                       scoped_refptr<QuicChromiumPacketWriter::ReusableIOBuffer>
                           last_packet) override;
  void OnWriteError(int error_code) override;
  void OnWriteUnblocked() override;

  // Starts probe |network| to |peer_address|. |this| will take ownership of
  // |socket|, |writer| and |reader|. |writer| and |reader| should be bound
  // to |socket| and |writer| will be used to send connectivity probing packets.
  // Connectivity probing packets will be resent after initial timeout. Mutilple
  // trials will be attempted with exponential backoff until a connectivity
  // probing packet response is received from the peer by |reader| or final
  // time out.
  void StartProbing(NetworkChangeNotifier::NetworkHandle network,
                    const QuicSocketAddress& peer_address,
                    std::unique_ptr<DatagramClientSocket> socket,
                    std::unique_ptr<QuicChromiumPacketWriter> writer,
                    std::unique_ptr<QuicChromiumPacketReader> reader,
                    base::TimeDelta initial_timeout,
                    const NetLogWithSource& net_log);

  // Cancels undergoing probing if the current |network_| being probed is the
  // same as |network|.
  void CancelProbing(NetworkChangeNotifier::NetworkHandle network);

  // Called when a connectivity probing packet has been received from
  // |peer_address| on a socket with |self_address|.
  void OnConnectivityProbingReceived(const QuicSocketAddress& self_address,
                                     const QuicSocketAddress& peer_address);

  // Returns true if the manager is currently probing |network| to
  // |peer_address|.
  bool IsUnderProbing(NetworkChangeNotifier::NetworkHandle network,
                      const QuicSocketAddress& peer_address) {
    return (network == network_ && peer_address == peer_address_);
  }

 private:
  // Cancels undergoing probing.
  void CancelProbingIfAny();

  // Called when a connectivity probing needs to be sent to |peer_address_| and
  // set a timer to resend a connectivity probing packet to peer after
  // |timeout|.
  void SendConnectivityProbingPacket(base::TimeDelta timeout);

  // Called when no connectivity probing packet response has been received on
  // the currrent probing path after timeout.
  void MaybeResendConnectivityProbingPacket();

  void NotifyDelegateProbeFailed();

  Delegate* delegate_;  // Unowned, must outlive |this|.
  NetLogWithSource net_log_;

  // Current network that is under probing, resets to
  // NetworkChangeNotifier::kInvalidNetwork when probing results has been
  // delivered to |delegate_|.
  NetworkChangeNotifier::NetworkHandle network_;
  QuicSocketAddress peer_address_;

  std::unique_ptr<DatagramClientSocket> socket_;
  std::unique_ptr<QuicChromiumPacketWriter> writer_;
  std::unique_ptr<QuicChromiumPacketReader> reader_;

  int64_t retry_count_;
  base::TimeDelta initial_timeout_;
  base::OneShotTimer retransmit_timer_;

  base::SequencedTaskRunner* task_runner_;

  base::WeakPtrFactory<QuicConnectivityProbingManager> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(QuicConnectivityProbingManager);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_QUIC_CONNECTIVITY_PROBING_MANAGER_H_
