# Hacking on the GN binary itself

## Building GN itself

GN is part of the Chromium tree, in [//tools/gn/](../). If you have a
Chromium checkout, you already have the source and you can do `ninja -C
out/Debug gn` to build it.

To build gn using gn, run (in the root `src` directory):

```
gn gen out/Default
ninja -C out/Default gn
```

Change `out/Default` as necessary to put the build directory where you
want.

## Running GN's unit tests

```
ninja -C out/Default gn_unittests && out/Default/gn_unittests
```
