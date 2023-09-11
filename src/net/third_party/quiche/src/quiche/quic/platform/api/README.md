# QUIC platform API

This directory contains the infrastructure blocks needed to support QUIC in
certain platform. These APIs act as interaction layers between QUIC core and
either the upper layer application (i.e. Chrome, Envoy) or the platform's own
infrastructure (i.e. logging, test framework and system IO). QUIC core needs the
implementations of these APIs to build and function appropriately. There is
unidirectional dependency from QUIC core to most of the APIs here, such as
QUIC_LOG and QuicMutex, but a few APIs also depend back on QUIC core's basic
QUIC data types, such as QuicClock and QuicSleep.

-   APIs used by QUIC core:

    Most APIs are used by QUIC core to interact with platform infrastructure
    (i.e. QUIC_LOG) or to wrap around platform dependent data types (i.e.
    QuicThread), the dependency is:

```
application -> quic_core -> quic_platform_api
      |                             |
      v                             v
platform_infrastructure <- quic_platform_impl
```

-   APIs used by applications:

    Some APIs are used by applications to interact with QUIC core (i.e.
    QuicMemSlice). For such APIs, their dependency model is:

```
application -> quic_core -> quic_platform_api
    |                            ^
    |                            |
     -------------------> quic_platform_impl
    |                            |
    |                            v
     -------------------> platform_infrastructure
```

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;An example for such dependency
is QuicClock.

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Or

```
application -> quic_core -> quic_platform_api
    |                            ^
    |                            |
    |                            v
     -------------------> quic_platform_impl
    |                            |
    |                            v
     -------------------> platform_infrastructure
```

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;An example for such dependency
is QuicMemSlice.

# Documentation of each API and its usage.

QuicMemSlice
:   QuicMemSlice is used to wrap application data and pass to QUIC stream's
    write interface. It refers to a memory block of data which should be around
    till QuicMemSlice::Reset() is called. It's upto each platform, to implement
    it as reference counted or not.

QuicClock
:   QuicClock is used by QUIC core to get current time. Its instance is created
    by applications and passed into QuicDispatcher and
    QuicConnectionHelperInterface.

TODO(b/131224336) add document for other APIs
