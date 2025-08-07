# In Android 12 (upcoming)

## New features
* Support Custom Allocators. This allows developers to instrument their
  applications to report memory allocations / frees that are not done
  through the malloc-based system allocators.

## Bugfixes
* Fix problems with allocations done in signal handlers using SA_ONSTACK.
* Fixed heapprofd for multi API. A 64-bit heapprofd service can now correctly
  profile a 32-bit target.
* Fixed a bug where specifying a sampling rate of 0 would crash the target
  process.

# In Android 11

## New features
* Allow to specify whether profiling should only be done for existing processes
  or only for newly spawned ones using `no_startup` or `no_running` in
  `HeapprofdConfig`.
* Allow to get the number of bytes that were allocated at a callstack but then
  not used.
* Allow to dump the maximum, rather than at the time of the dump using
  `dump_at_max` in `HeapprofdConfig`.
* Allow to specify timeout (`block_client_timeout_us`) when blocking mode is
  used. This will tear down the profile if the client would be blocked for
  longer than this.
* Try to auto-detect if a process uses `vfork(2)` or `clone(2)` with
  `CLONE_VM`. In Android 10, doing memory operations in a vfork-ed child (in
  violation of POSIX) would tear down the parent's profiling session early.

## Bugfixes
* Fixed heapprofd on x86.
* Fixed issue with calloc being incorrectly sampled.
* Remove benign `ERROR 2` bottom-most frame on ARM32.
