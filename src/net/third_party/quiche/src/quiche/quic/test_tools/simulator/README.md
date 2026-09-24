# QUIC network simulator

This directory contains a discrete event network simulator which QUIC code uses
for testing congestion control and other transmission control code that requires
a network simulation for tests on QuicConnection level of abstraction.

## Actors

The core of the simulator is the Simulator class, which maintains a virtual
clock and an event queue. Any object in a simulation that needs to schedule
events has to subclass Actor. Subclassing Actor involves:

1.  Calling the `Actor::Actor(Simulator*, std::string)` constructor to establish
    the name of the object and the simulator it is associated with.
2.  Calling `Schedule(QuicTime)` to schedule the time at which `Act()` method is
    called. `Schedule` will only cause the object to be rescheduled if the time
    for which it is currently scheduled is later than the new time.
3.  Implementing `Act()` method with the relevant logic. The actor will be
    removed from the event queue right before `Act()` is called.

Here is a simple example of an object that outputs simulation time into the log
every 100 ms.

```c++
class LogClock : public Actor {
 public:
  LogClock(Simulator* simulator, std::string name) : Actor(simulator, name) {
    Schedule(clock_->Now());
  }
  ~LogClock() override {}

  void Act() override {
    QUIC_LOG(INFO) << "The current time is "
                   << clock_->Now().ToDebuggingValue();
    Schedule(clock_->Now() + QuicTime::Delta::FromMilliseconds(100));
  }
};
```

A QuicAlarm object can be used to schedule events in the simulation using
`Simulator::GetAlarmFactory()`.

## Ports

The simulated network transfers packets, which are modelled as an instance of
struct `Packet`. A packet consists of source and destination address (which are
just plain strings), a transmission timestamp and the UDP-layer payload.

The simulation uses the push model: any object that wishes to transfer a packet
to another component in the simulation has to explicitly do it itself. Any
object that can accept a packet is called a *port*. There are two types of
ports: unconstrained ports, which can always accept packets, and constrained
ports, which signal when they can accept a new packet.

An endpoint is an object that is connected to the network and can both receive
and send packets. In our model, the endpoint always receives packets as an
unconstrained port (*RX port*), and always writes packets to a constrained port
(*TX port*).

## Links

The `SymmetricLink` class models a symmetric duplex links with finite bandwidth
and propagation delay. It consists of a pair of identical `OneWayLink`s, which
accept packets as a constrained port (where constrain comes from the finiteness
of bandwidth) and outputs them into an unconstrained port. Two endpoints
connected via a `SymmetricLink` look like this:

```none
 Endpoint A                                                 Endpoint B
+-----------+                 SymmetricLink                +-----------+
|           |       +------------------------------+       |           |
|      +---------+  |  +------------------------+  |  +---------+      |
|      | RX port <-----|       OneWayLink      *<-----| TX port |      |
|      +---------+  |  +------------------------+  |  +---------+      |
|           |       |                              |       |           |
|      +---------+  |  +------------------------+  |  +---------+      |
|      | TX port |----->*      OneWayLink       |-----> RX port |      |
|      +---------+  |  +------------------------+  |  +---------+      |
|           |       +------------------------------+       |           |
+-----------+                                              +-----------+

                    ( -->* denotes constrained port)
```

In most common scenario, one of the endpoints is going to be a QUIC endpoint,
and another is going to be a switch port.

## Other objects

Besides `SymmetricLink`, the simulator provides the following objects:

*   `Queue` allows to convert a constrained port into an unconstrained one by
    buffering packets upon arrival. The queue has a finite size, and once the
    queue is full, the packets are silently dropped.
*   `Switch` simulates a multi-port learning switch with a fixed queue for each
    output port.
*   `QuicEndpoint` allows QuicConnection to be run over the simulated network.
*   `QuicEndpointMultiplexer` allows multiple connections to share the same
    network endpoint.
