This directory has the following layout (WIP):
- base/task/: public APIs for posting tasks and managing task queues.
- base/task/task_scheduler/: implementation of the TaskScheduler.
- base/task/sequence_manager/: implementation of the SequenceManager.

Apart from embedders explicitly managing a TaskScheduler and/or SequenceManager
instance(s) for their process/threads, the vast majority of users should only
need APIs in base/task/.

Documentation:
- [Threading and tasks](https://chromium.googlesource.com/chromium/src/+/lkcr/docs/threading_and_tasks.md)
- [Callbacks](https://chromium.googlesource.com/chromium/src/+/lkcr/docs/callback.md)
