# CheckedPtr

`CheckedPtr<T>` is a smart pointer that triggers a crash when dereferencing a
dangling pointer.  It is currently considered **experimental** - please don't
use it in production code just yet.

## Benefits and costs of CheckedPtr

TODO: Expand the raw notes below:
- Benefit = making UaF bugs non-exploitable
  - Protected
    - dereference (null not ok): `operator*`, `operator->`
    - extraction (null ok): `.get()`, implicit casts
  - Not protected:
    - comparison: `operator==`, etc.
    - maybe middle-of-allocation-pointers
    - stack pointers
      (and pointers to other non-PartitionAlloc-managed allocations)
- Cost = performance hit
  (TODO: point to preliminary performance results)


## Fields should use CheckedPtr rather than raw pointers

Eventually, once CheckedPtr is no longer **experimental**,
fields (aka member variables) in Chromium code
should use `CheckedPtr<SomeClass>` rather than raw pointers.

TODO: Expand the raw notes below:
- Chromium-only (V8, Skia, etc. excluded)
- Fields-only
  (okay to use raw pointer variables, params, container elements, etc.)
- TODO: Explain how this will be eventually enforced (presubmit? clang plugin?).
  Explain how to opt-out (e.g. see "Incompatibilities with raw pointers"
  section below where some scenarios are inherently incompatible
  with CheckedPtr.


## Incompatibilities with raw pointers

In most cases, changing the type of a field
(or a variable, or a parameter, etc.)
from `SomeClass*` to `CheckedPtr<SomeClass>`
shouldn't require any additional changes - all
other usage of the pointer should continue to
compile and work as expected at runtime.

There are some corner-case scenarios however,
where `CheckedPtr<SomeClass>` is not compatible with a raw pointer.
Subsections below enumerate such scenarios
and offer guidance on how to work with them.

### Compile errors

#### Explicit `.get()` might be required

If a raw pointer is needed, but an implicit cast from
`CheckedPtr<SomeClass>` to `SomeClass*` doesn't work,
then the raw pointer needs to be obtained by explicitly
calling `.get()`.  Examples:

- `auto* raw_ptr_var = checked_ptr.get()`
  (`auto*` requires the initializer to be a raw pointer)
- `return condition ? raw_ptr : checked_ptr.get();`
  (ternary operator needs identical types in both branches)
- `base::WrapUniquePtr(checked_ptr.get());`
  (implicit cast doesn't kick in for arguments in templates)
- `printf("%p", checked_ptr.get());`
  (can't pass class type arguments to variadic functions)
- `reinterpret_cast<SomeClass*>(checked_ptr.get())`
  (`const_cast` and `reinterpret_cast` sometimes require their
  argument to be a raw pointer;  `static_cast` should "Just Work")

#### In-out arguments need to be refactored

Due to implementation difficulties,
`CheckedPtr` doesn't support an address-of operator.
This means that the following code will not compile:

```cpp
    void GetSomeClassPtr(SomeClass** out_arg) {
      *out_arg = ...;
    }

    struct MyStruct {
      void Example() {
        GetSomeClassPtr(&checked_ptr_);  // <- won't compile
      }

      CheckedPtr<SomeClass> checked_ptr_;
    };
```

The typical fix is to change the type of the out argument:

```cpp
    void GetSomeClassPtr(CheckedPtr<SomeClass>* out_arg) {
      *out_arg = ...;
    }
```

If `GetSomeClassPtr` can be invoked _both_ with raw pointers
and with `CheckedPtr`, then both overloads might be needed:

```cpp
    void GetSomeClassPtr(SomeClass** out_arg) {
      *out_arg = ...;
    }

    void GetSomeClassPtr(CheckedPtr<SomeClass>* out_arg) {
      SomeClass* tmp = **out_arg;
      GetSomeClassPtr(&tmp);
      *out_arg = tmp;
    }
```

#### No `constexpr` for non-null values

`constexpr` raw pointers can be initialized with pointers to string literals
or pointers to global variables.  Such initialization doesn't work for
CheckedPtr which doesn't have a `constexpr` constructor for non-null pointer
values.

If `constexpr`, non-null initialization is required, then the only solution is
avoiding `CheckedPtr`.

### Runtime crashes

#### Special sentinel values

`CheckedPtr` cannot be assigned special sentinel values like
`reinterpret_cast<void*>(-2)`.
Using such values with `CheckedPtr` will lead to crashes at runtime
(`CheckedPtr` would crash when attempting to read the memory tag from
the "allocation" at the fake sentinel address).

Example where this happens in practice:
[reinterpret_cast here](https://source.chromium.org/chromium/chromium/src/+/master:base/threading/thread_local_storage.cc;l=153;drc=c3cffa634ce1fd84baaab5ba507e240b8abbd977)
might try to convert `-2` (from
[kPerfFdDisabled](https://source.chromium.org/chromium/chromium/src/+/master:base/trace_event/thread_instruction_count.cc;l=28;drc=9a7c42e7b3ce922f16b308e2b295f109b56b9fa2))
into `void*`.

Suggested solution is to use `uintptr_t` instead of `void*` for storing
non-pointer values (e.g. `-2` sentinel value).


#### Dangling CheckedPtr may crash without a dereference

A dangling raw pointer can be passed as an argument
(or assigned to other variables, etc.) without necessarily
triggering an undefined behavior (as long as the dangling
pointer is not actually dereferenced).
OTOH, `CheckedPtr` safety checks will kick in whenever `CheckedPtr`
is converted to a raw pointer (e.g. when passing `CheckedPtr`
to a function that takes a raw pointer as a function argument).

Example where this happens in practice:
[WaitableEventWatcher::StopWatching](https://source.chromium.org/chromium/chromium/src/+/master:base/synchronization/waitable_event_watcher_posix.cc;l=165;drc=c3cffa634ce1fd84baaab5ba507e240b8abbd977)
may be dealing with a dangling `waiter_` pointer.

TODO:
- What to do (avoid CheckedPtr? introduce and use `UnsafeGet()` method?)
- Generic guidance (can we say that this is an inherently dangerous situation
  and should be avoided in general?)


## Other notes

### Unions mixing raw pointers and CheckedPtr

C++ standard [says](https://en.cppreference.com/w/cpp/language/union) that
"it's undefined behavior to read from the member of the union that wasn't most
recently written".
As long as only the most recently written union member is used, it should be
okay to use `CheckedPtr` in a union (even in a situation where a mix of raw
pointer fields and `CheckedPtr` fields is present in the same union).
