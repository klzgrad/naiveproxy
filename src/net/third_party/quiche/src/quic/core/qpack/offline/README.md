# QPACK Offline Interop Testing tools

See
[QPACK Offline Interop](https://github.com/quicwg/base-drafts/wiki/QPACK-Offline-Interop)
for description of test data format.

Example usage:

```shell
$ # Download test data
$ cd $TEST_DATA
$ git clone https://github.com/qpackers/qifs.git
$ TEST_ENCODED_DATA=`pwd`/qifs/encoded/qpack-03
$ TEST_QIF_DATA=`pwd`/qifs/qifs
$
$ # Decode encoded test data in four files and verify that they match
$ # the original headers in corresponding files
$ $BIN/qpack_offline_decoder \
>   $TEST_ENCODED_DATA/f5/fb-req.qifencoded.4096.100.0 \
>   $TEST_QIF_DATA/fb-req.qif
>   $TEST_ENCODED_DATA/h2o/fb-req-hq.out.512.0.1 \
>   $TEST_QIF_DATA/fb-req-hq.qif
>   $TEST_ENCODED_DATA/ls-qpack/fb-resp-hq.out.0.0.0 \
>   $TEST_QIF_DATA/fb-resp-hq.qif
>   $TEST_ENCODED_DATA/proxygen/netbsd.qif.proxygen.out.4096.0.0 \
>   $TEST_QIF_DATA/netbsd.qif
$
```
