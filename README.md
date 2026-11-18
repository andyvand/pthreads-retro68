# pthreads-retro68

POSIX threads for classic Mac OS, built with the [Retro68](https://github.com/autc04/Retro68)
toolchain and implemented on top of the **Thread Manager**.

| Toolchain (`CMAKE_SYSTEM_NAME`) | Library               | Thread Manager access          |
|---------------------------------|-----------------------|--------------------------------|
| `Retro68` (68k)                 | `libpthread.a`        | `_ThreadDispatch` A-trap       |
| `RetroPPC` (PowerPC, CFM)       | `libpthread.a`        | `ThreadsLib` import library    |
| `RetroCarbon` (PowerPC, Carbon) | `libpthread-carbon.a` | `CarbonLib`                    |

## Building and installing

```sh
# 68k
cmake -S . -B build-68k \
  -DCMAKE_TOOLCHAIN_FILE=/opt/retro68/m68k-apple-macos/cmake/retro68.toolchain.cmake
cmake --build build-68k && cmake --install build-68k

# PowerPC (classic)
cmake -S . -B build-ppc \
  -DCMAKE_TOOLCHAIN_FILE=/opt/retro68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
cmake --build build-ppc && cmake --install build-ppc

# PowerPC (Carbon)
cmake -S . -B build-carbon \
  -DCMAKE_TOOLCHAIN_FILE=/opt/retro68/powerpc-apple-macos/cmake/retrocarbon.toolchain.cmake
cmake --build build-carbon && cmake --install build-carbon
```

The toolchain files set the install prefix to `/opt/retro68/<triple>/`. The files go here:

```
<prefix>/include/pthreads-retro68/pthread.h, semaphore.h
<prefix>/lib/libpthread.a                (libpthread-carbon.a for Carbon)
<prefix>/lib/pkgconfig/pthread.pc        (pthread-carbon.pc)
<prefix>/lib/cmake/pthreads-retro68/     package config
<prefix>/lib/cmake/Threads/              ThreadsConfig.cmake
<prefix>/cmake/FindThreads.cmake         FindThreads override (see below)
```

Options:

* `PTHREADS_RETRO68_BUILD_EXAMPLES` (ON): builds `PThreadTest`, a console
  self-test application (`examples/pthread_test.c`).
* `PTHREADS_RETRO68_INSTALL_FINDTHREADS` (ON): installs the `FindThreads.cmake` override.

The headers go in their own subdirectory so they don't replace newlib's
`pthread.h`, which is compiled out for these targets. The include path
comes from CMake or pkg-config.

## Using it

### CMake: `find_package(Threads)`

```cmake
find_package(Threads REQUIRED)
add_application(MyApp main.c)
target_link_libraries(MyApp Threads::Threads)
```

The Retro68 toolchain files add `<prefix>/cmake` to `CMAKE_MODULE_PATH`.
That makes the installed `FindThreads.cmake` take precedence over CMake's own
module. It sets the usual variables (`Threads_FOUND`,
`CMAKE_USE_PTHREADS_INIT`, `CMAKE_THREAD_LIBS_INIT`) and defines
`Threads::Threads`. `find_package(Threads CONFIG)` also works.

> Retro68's `add_application()` calls `target_link_libraries` with the plain
> signature. Use the plain form too (no `PRIVATE` or `PUBLIC`), or CMake will
> report an error about mixing signatures.

### CMake: the package directly

```cmake
find_package(pthreads-retro68 1.0 REQUIRED)
target_link_libraries(MyApp pthreads-retro68::pthread)
```

The targets file is chosen to match the consumer's `CMAKE_SYSTEM_NAME`, so
PPC and Carbon projects that share `powerpc-apple-macos` each get the right
library.

### pkg-config

```sh
export PKG_CONFIG_LIBDIR=/opt/retro68/m68k-apple-macos/lib/pkgconfig
m68k-apple-macos-gcc $(pkg-config --cflags pthread) main.c $(pkg-config --libs pthread)
# Carbon: pkg-config pthread-carbon
```

## What is implemented

* Threads: `pthread_create`, `join`, `detach`, `exit`, `self`, `equal`,
  `once`, `yield`, `pthread_setname_np` and `pthread_getname_np`. Attributes
  cover detach state, stack size, scope, inherit, policy and param.
* Mutexes: normal, recursive, error-checking and default, plus `trylock`,
  `timedlock` and `clocklock`. Static initializers include
  `PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP`.
* Condition variables: `wait`, `timedwait` and `clockwait`, with
  `CLOCK_REALTIME` or `CLOCK_MONOTONIC` set through `pthread_condattr_setclock`.
* Read/write locks (writer-preferring), barriers, and spin locks.
* Thread-specific data: 64 keys, with destructors run at thread exit.
* Deferred cancellation with cleanup handlers.
* Cancellation points: `pthread_join`, `pthread_cond_*wait`, `pthread_testcancel`,
  `sem_*wait`, `nanosleep`, `clock_nanosleep`, `usleep` and `sleep`.
* Unnamed POSIX semaphores (`<semaphore.h>`).
* `sched_yield`, plus `clock_gettime`, `clock_getres`, `nanosleep`,
  `clock_nanosleep`, `usleep` and `sleep`. libc declares these but doesn't
  implement them. They are defined as weak symbols, and the sleep functions
  yield to other threads.
* `pthread_mac_threadid_np()` returns the Thread Manager `ThreadID` behind a
  `pthread_t`.

## Semantics and limitations

* **Scheduling is cooperative.** All threads are Thread Manager cooperative
  threads. A thread keeps the CPU until it blocks in this library, calls
  `sched_yield()` or `pthread_yield()`, or sleeps. A thread that runs a long
  computation without doing any of these starves all the others, including
  the main event loop. Long-running workers should call `sched_yield()` now
  and then. The main thread should also yield in its event loop so threads
  can run. Calling `WaitNextEvent` alone is not enough.
* **Blocking:** threads created with `pthread_create` block by putting
  themselves into the Thread Manager's *stopped* state, so they use no CPU.
  The main application thread, and any thread not created by this library,
  never stops itself. While it waits it keeps calling `YieldToAnyThread()`,
  so the application stays responsive to the Process Manager. Timed waits
  also poll by yielding.
* Preemptive Thread Manager threads are not used. On 68k they could not
  call the Memory Manager or most of the Toolbox anyway.
* `errno` and other newlib reentrancy state are shared by all threads. This
  only matters if a thread yields between setting `errno` and reading it.
* The Thread Manager allocates thread stacks. `pthread_attr_setstack` and
  `pthread_attr_setstackaddr` return `ENOTSUP`. The default stack size is
  64 KB (`PTHREADS_RETRO68_DEFAULT_STACKSIZE`). Use
  `pthread_attr_setstacksize` to change it.
* The Thread Manager has no thread priorities. Only priority 0 is accepted.
  `PTHREAD_PROCESS_SHARED`, `PTHREAD_SCOPE_SYSTEM` and the priority-inheritance
  protocols all return `ENOTSUP`.
* `pthread_exit` and cancellation unwind with `longjmp`. C++ destructors of
  objects on the exiting thread's stack are **not** run.
* `CLOCK_REALTIME` uses Retro68's `gettimeofday` (tick-based, 1/60 s).
  `CLOCK_MONOTONIC` uses `Microseconds()`.
* Zero-filled mutex, condition variable and rwlock objects are treated like
  the static initializers. The object behind them is allocated on first use.
* There is no `fork()`, so `pthread_atfork` just returns 0. Named semaphores
  return `ENOSYS`.
* Requirements: the Thread Manager, built into System 7.5 and later or
  available as an extension on System 7.0/7.1. On 68k it is detected through
  Gestalt (`'thds'`). Without it, `pthread_create` returns `EAGAIN` and
  everything else works single-threaded. On classic PowerPC the application
  hard-imports `ThreadsLib`.
* libstdc++ in Retro68 is built without gthreads, so `std::thread` is still
  unavailable. Use pthreads directly.
