This directory contains several fuzz tests for QUIC code:

-   quic_framer_fuzzer: A test for CryptoFramer::ParseMessage and
    QuicFramer::ProcessPacket using random packet data.
-   quic_framer_process_data_packet_fuzzer: A test for QuicFramer::ProcessPacket
    where the packet has a valid public header, is decryptable, and contains
    random QUIC payload.

To build and run the fuzz tests, using quic_framer_fuzzer as an example:

```sh
$ blaze build --config=asan-fuzzer //gfe/quic/test_tools/fuzzing/...
$ CORPUS_DIR=`mktemp -d` && echo ${CORPUS_DIR}
$ ./blaze-bin/gfe/quic/test_tools/fuzzing/quic_framer_fuzzer ${CORPUS_DIR} -use_counters=0
```

By default this fuzzes with 64 byte chunks, to test the framer with more
realistic size input, try 1350 (max payload size of a QUIC packet):

```sh
$ ./blaze-bin/gfe/quic/test_tools/fuzzing/quic_framer_fuzzer ${CORPUS_DIR} -use_counters=0 -max_len=1350
```
