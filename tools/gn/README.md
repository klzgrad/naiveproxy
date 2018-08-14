# GN

GN is a meta-build system that generates build files for
[Ninja](https://ninja-build.org).

## Getting started

    git clone https://gn.googlesource.com/gn
    cd gn
    python build/gen.py
    ninja -C out

On Windows, it is expected that `cl.exe`, `link.exe`, and `lib.exe` can be found
in `PATH`, so you'll want to run from a Visual Studio command prompt, or
similar.

On Linux and Mac, the default compiler is `clang++`, a recent version is
expected to be found in `PATH`. This can be overridden by setting `CC`, `CXX`,
and `AR`.

## Sending patches

GN uses [Gerrit](https://www.gerritcodereview.com/) for code review. The short
version of how to patch is:

    ... edit code ...
    ninja -C out && out/gn_unittests

Then, to upload a change for review:

    git commit
    git push origin HEAD:refs/for/master  # This uploads for review.

When revising a change, use:

    git commit --amend
    git push origin HEAD:refs/for/master

which will add the new changes to the existing code review, rather than creating
a new one.

We ask that all contributors
[sign Google's Contributor License Agreement](https://cla.developers.google.com/)
(either individual or corporate as appropriate, select 'any other Google
project').

## Community

You may ask questions and follow along w/ GN's development on Chromium's
[gn-dev@](https://groups.google.com/a/chromium.org/forum/#!forum/gn-dev)
Google Group.
