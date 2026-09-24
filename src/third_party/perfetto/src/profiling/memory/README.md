# heapprofd - Android Heap Profiler

heapprofd allows you to attribute memory usage to functions for Android services
and apps.

For documentation, see https://perfetto.dev/docs/data-sources/native-heap-profiler.

For design doc, see https://perfetto.dev/docs/design-docs/heapprofd-design.

## GN Targets
### Factories
android: connects to the system heapprofd. The client API will need to have
         been built at *exactly* the same version. This means this can only
         be used by the API shipped in the platform.

standalone: executes an in-process heapprofd. Can be used on old platform
            versions.

noop: ignores all calls to the client API. This can be used as a stand-in when
      compiling, or when executing without profiling.

### Interceptors
bionic: uses bionic [malloc dispatch](
https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/private/bionic_malloc_dispatch.h)
to intercept allocation functions on Android. This works by placing a library
on a pre-defined path, which gets [loaded by Bionic](
https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/bionic/malloc_heapprofd.cpp).

glibc: generates a library exposing the allocation functions. This library
       should be used for `LD_PRELOAD` and uses the glibc specific symbols
       like `__libc_malloc` to use the system allocator.

### Shared libraries

| GN target                   | factory    | interceptor | distribution |
|-----------------------------|------------|-------------|--------------|
| heapprofd_client            | android    | bionic      | platform     |
| heapprofd_client_api        | android    | none        | platform     |
| heapprofd_glibc_preload     | standalone | glibc       | unbundled    |
| heapprofd_standalone_client | standalone | none        | unbundled    |
| heapprofd_api_noop          | noop       | none        | unbundled    |


## Heap profile heapprofd

For development, you might want to get a heap profile of heapprofd while it
is profiling something else. For that reason, we allow two heapprofds to run
on the system. The secondary heapprofd can then profile your primary one.

To do this, first make sure that heapprofd is running by setting the property

```
adb shell su root setprop persist.heapprofd.enable 1
```

Take note of its PID.

```
adb shell ps -e | grep heapprofd
```

Then, move away the primary heapprofd socket to make space for the secondary
one

```
adb shell su root mv /dev/socket/heapprofd /dev/socket/heapprofd_primary
```

Start the secondary heapprofd

```
adb shell su root start heapprofd_secondary
```

Now we can start the profile of the primary heapprofd (using the secondary).
Leave this session running.

```
tools/heap_profile -p ${PID_FROM_ABOVE}
```

Now move back the original socket

```
adb shell su root unlink /dev/socket/heapprofd
adb shell su root mv /dev/socket/heapprofd_primary /dev/socket/heapprofd
```

Now all subsequent profiles will be done on the primary heapprofd again, with
the secondary observing it.
