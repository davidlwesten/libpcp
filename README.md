# libpcp

A client library for the Port Control Protocol ([RFC 6887]), written in C23.

PCP lets a host ask its router or firewall to open a port for incoming
traffic, much like NAT-PMP or UPnP. This library implements the client side:
it builds requests, retransmits them, renews leases and tells the application
when a mapping is up or gone.

[RFC 6887]: https://www.rfc-editor.org/rfc/rfc6887

## Sans-IO

The library never touches the network or the clock. The application owns the
socket, the time and the source of randomness, and the library is a state
machine in between:

- the application hands it received datagrams and the current time
- the library tells it what to send and how long it can sleep

That keeps the library small and deterministic. It works with any event loop
or socket API, and the tests can drive it through hours of retransmissions and
renewals without waiting for anything.

## Features

- MAP and PEER mappings, with the THIRD_PARTY and FILTER options
- Retransmissions with exponential backoff and jitter
- Lease renewals, keeping the same external address when possible
- Detecting server restarts, including unsolicited ANNOUNCE messages
- Deleting mappings when the application releases them
- No allocations after a mapping is created, a custom allocator can be
  plugged in

## Usage

The application calls `pcp_session_poll` until it returns
`PCP_EVENT_TIMEOUT`, handles the events in between and then waits for a
datagram or the deadline:

```c
uint8_t buffer[PCP_PACKET_MAX_SIZE];
pcp_session_t *session = pcp_session_begin((pcp_config_t){
    .local = local_address, // our address as the server sees it
    .rng = {.fill = random_bytes},
    .buffer = {.size = sizeof(buffer), .data = buffer},
});

auto const handle =
    pcp_session_map_request(session, 3600, UDP, 12345, 0, nullptr);

while (running) {
  pcp_event_t event;
  do {
    event = pcp_session_poll(session, now_ms());
    switch (event.type) {
    case PCP_EVENT_SEND:
      send(socket, event.send.data, event.send.size, 0);
      break;
    case PCP_EVENT_MAPPED:
      // reachable at event.mapped.external
      break;
    case PCP_EVENT_UNMAPPED:
    case PCP_EVENT_FAILED:
    case PCP_EVENT_TIMEOUT:
      break;
    }
  } while (event.type != PCP_EVENT_TIMEOUT);

  // wait for a datagram or until event.timeout.deadline, and hand any datagram
  // to pcp_session_on_datagram(session, now_ms(), size, data)
}

pcp_session_end(session);
```

`example/` has a complete client using a UDP socket and `select`, including
releasing its mapping on Ctrl-C.

## Building

libpcp needs a C23 compiler and CMake 3.23 or newer. CI builds with GCC 14 and
Clang 20.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The example client takes the server's address:

```sh
./build/example/pcpc 192.168.1.1:5351
```

### Using it from CMake

With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(
  libpcp
  GIT_REPOSITORY https://github.com/davidlwesten/libpcp.git
  GIT_TAG main
)
FetchContent_MakeAvailable(libpcp)

target_link_libraries(your_target PRIVATE pcp::pcp)
```

The tests and example are only built when libpcp is the top-level project. On
its own, the tests can be turned off with the usual `BUILD_TESTING` and the
example with `PCP_BUILD_EXAMPLE`.

## License

MIT, see [LICENSE](LICENSE).
