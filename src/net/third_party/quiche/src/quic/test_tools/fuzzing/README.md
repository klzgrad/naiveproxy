Examples of fuzz testing QUIC code using libfuzzer (go/libfuzzer).

To build and run the examples:

```sh
$ blaze build --config=asan-fuzzer //gfe/quic/test_tools/fuzzing/...
$ CORPUS_DIR=`mktemp -d` && echo ${CORPUS_DIR}
$ ./blaze-bin/gfe/quic/test_tools/fuzzing/quic_framer_fuzzer ${CORPUS_DIR} -use_counters=0
```

By default this fuzzes with 64 byte chunks, to test the framer with more realistic
size input, try 1350 (max payload size of a QUIC packet):

```sh
$ ./blaze-bin/gfe/quic/test_tools/fuzzing/quic_framer_fuzzer ${CORPUS_DIR} -use_counters=0 -max_len=1350
```
