#include "net/third_party/quic/platform/impl/quic_port_utils_impl.h"

#include "net/third_party/quic/core/crypto/quic_random.h"

namespace quic {

int QuicPickUnusedPortOrDieImpl() {
  return 12345 + (QuicRandom::GetInstance()->RandUint64() % 20000);
}

void QuicRecyclePortImpl(int port) {}

}  // namespace quic
