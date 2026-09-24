# Net Module

This directory contains `NetModule` and functionality that depends on embedding
application resources and mechanics (such as directory listing HTML snippets).

## Design Philosophy

The purpose of isolating these files into a standalone directory and build
target (`//net/base/module`) is to maintain cleaner component boundaries. This
enables lightweight consumers (such as iOS app extensions) to depend on core
utilities in `//net/base` while using `assert_no_deps` to explicitly restrict
`//net/base/module`, thereby preventing unneeded resources or embedding
mechanics from being pulled into constrained binaries.

To preserve this modularity, the contents of this directory should be kept to an
absolute minimum. New utility functions and core networking classes should
generally not be added here.
