#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_PORT_UTILS_IMPL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_PORT_UTILS_IMPL_H_

namespace quic {

int QuicPickUnusedPortOrDieImpl();
void QuicRecyclePortImpl(int port);

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_PORT_UTILS_IMPL_H_
