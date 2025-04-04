# What is this
Contains a written down set of principles and other information on //base.
Please add to it!

## About //base:

Chromium is a very mature project. Most things that are generally useful are
already here and things not here aren't generally useful.

The bar for adding stuff to base is that it must have demonstrated wide
applicability. Prefer to add things closer to where they're used (i.e. "not
base"), and pull into base only when needed. In a project our size,
sometimes even duplication is OK and inevitable.

Adding a new logging macro `DPVELOG_NE` is not more clear than just
writing the stuff you want to log in a regular logging statement, even
if it makes your calling code longer. Just add it to your own code.

If the code in question does not need to be used inside base, but will have
multiple consumers across the codebase, consider placing it in a new directory
under components/ instead.

base is written for the Chromium project and is not intended to be used
outside it.  Using base outside of src.git is explicitly not supported,
and base makes no guarantees about API (or even ABI) stability (like all
other code in Chromium).  New code that depends on base/ must be in
src.git. Code that's not in src.git but pulled in through DEPS (for
example, v8) cannot use base.

## Qualifications for being in //base OWNERS
  * interest and ability to learn low level/high detail/complex c++ stuff
  * inclination to always ask why and understand everything (including external
    interactions like win32) rather than just hoping the author did it right
  * mentorship/experience
  * demonstrated good judgement (esp with regards to public APIs) over a length
    of time

Owners are added when a contributor has shown the above qualifications and
when they express interest. There isn't an upper bound on the number of OWNERS.

## Design and naming
  * Be sure to use the base namespace.
  * STL-like constructs should adhere as closely to STL as possible. Functions
    and behaviors not present in STL should only be added when they are related
    to the specific data structure implemented by the container.
  * For STL-like constructs our policy is that they should use STL-like naming
    even when it may conflict with the style guide. So functions and class names
    should be lower case with underscores. Non-STL-like classes and functions
    should use Google naming.

## Performance testing

Since the primitives provided by //base are used very widely, it is important to
ensure they scale to the necessary workloads and perform well under all
supported platforms. The `base_perftests` target is a suite of
synthetic microbenchmarks that measure performance in various scenarios:

  * BasicPostTaskPerfTest: Exercises MessageLoopTaskRunner's multi-threaded
    queue in isolation.
  * ConditionVariablePerfTest: Measures thread switching cost of condition
    variables.
  * IntegratedPostTaskPerfTest: Exercises the full MessageLoop/RunLoop
    machinery.
  * JSONPerfTest: Tests JSONWriter and JSONReader performance.
  * MessageLoopPerfTest: Measures the speed of task posting in various
    configurations.
  * ObserverListPerfTest: Exercises adding, removing and signalling observers.
  * PartitionLockPerfTest: Tests the implementation of Lock used in
    PartitionAlloc
  * PthreadEventPerfTest: Establishes the baseline thread switching cost using
    pthreads.
  * RandUtilPerfTest: Measures the time it takes to generate random numbers.
  * ScheduleWorkTest: Measures the overhead of MessagePump::ScheduleWork.
  * SequenceManagerPerfTest: Benchmarks SequenceManager scheduling with various
    underlying task runners.
  * TaskObserverPerfTest: Measures the incremental cost of adding task
    observers.
  * TaskPerfTest: Checks the cost of posting tasks between threads.
  * ThreadLocalStoragePerfTest: Exercises different mechanisms for accessing
    data associated with the current thread (C++ `thread_local`, the
    implementation in //base, the POSIX/WinAPI directly)
  * WaitableEvent{Thread,}PerfTest: Measures waitable events in single and
    multithreaded scenarios.

Regressions in these benchmarks can generally by caused by 1) operating system
changes, 2) compiler version or flag changes or 3) changes in //base code
itself.
