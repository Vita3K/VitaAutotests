# vita-autotests

Small PS Vita homebrew tests, each checking one area of system behavior, with the output a real Vita gives. They can be useful to check an emulator against, or to understand how the system behaves.

The tests so far cover the kernel thread manager and its sync primitives, memory blocks, file I/O, save data slots, fibers, system modules, audio output, the display, the RTC library, and the sceClib string, format and memory space calls. Other areas can be added in the same format.

A test prints one line per value it observes, like `lock with a count of 0: 0x8002000F`, and ends with `DONE`. A run passes when the test exits with 0 and its output matches the `.expected` file next to its source. Tests print errors, counts and comparisons, never UIDs, addresses or times, so the output is the same on every run.

## Build

Needs vitasdk, with `VITASDK` set.

```
cmake -B build -G Ninja
cmake --build build
```

Every test goes into one vpk, `build/tests/vita-autotests.vpk`, with title ID `VTST00000`. Each launch runs one test in a fresh process: the one named by the first argument, by the `arg` of the launch URI, or by the first line of `ux0:data/vita-autotests/test.txt`. The name `list` prints every test.

A test prints each line to the debug output and to `ux0:data/vita-autotests/output/<name>.txt`. A Vita's debug output can drop lines when a test prints a lot at once, but the file never does. Each line is synced as it is written, so the file keeps everything even when a test hangs and gets killed.

A test that starts waiter threads waits for each one with `test_wait_until_waiting()`, and the waiters give up after `TEST_WAIT_US`. Starting a thread can take a while, so a fixed short delay isn't enough.

## Run on a Vita

Install the vpk and pick a test in one of two ways:

- Launch it from another app with the URI `psgm:play?titleid=VTST00000&arg=<name>`. This runs the `mutex` test, the same way VitaShell launches apps:

  ```c
  const char *uri = "psgm:play?titleid=VTST00000&arg=mutex";
  sceAppMgrLaunchAppByUri(0xFFFFF, uri);
  sceKernelDelayThread(10000);
  sceAppMgrLaunchAppByUri(0xFFFFF, uri);
  sceKernelExitProcess(0);
  ```

- Write the name to `ux0:data/vita-autotests/test.txt` and launch the app normally, like from the LiveArea.

When it closes, `ux0:data/vita-autotests/output/<name>.txt` holds the output, which is what the test's `.expected` file should hold.

## Run on an emulator

Pass the test name as the first argument, and compare the debug output with the test's `.expected` file.

## Add a test

Write `tests/<area>/<name>.c` with `PRINT`, `PRINTF` and `test_exit()` from `common/test.h`, and add it to `tests/CMakeLists.txt`. Run it on a Vita and save its output as `tests/<area>/<name>.expected`.

Cases that pass NULL pointers or huge sizes go at the end of a test, so an implementation that crashes on them loses nothing else. An info call given a size past its struct gets a buffer with room behind it, in case it writes that far.

A case that a wrong implementation may crash or hang on, like `wait_own_end`, goes in a test of its own, so it doesn't lose the output of the checks after it.

The output must not depend on timing, so it is the same however many times the test runs:

- A test never sleeps a fixed time to let another thread get somewhere. It waits for what that thread records, or for its end, with `TEST_WAIT_FOR()` or a join.
- A value is only read while every other thread of the test is blocked or has ended. Where the test expects a wake to do nothing, it reads right after the call that would have done it.
- Times are printed as comparisons, with every threshold far from each outcome the test tells apart. A timer event that must have fired gets at least ten times its interval.
