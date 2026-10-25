# vita-autotests

Small PS Vita homebrew tests, each checking one area of system behavior, with the output a real Vita gives. They can be useful to check an emulator against, or to understand how the system behaves.

The tests so far cover the kernel thread manager and its sync primitives, memory blocks, file I/O, save data slots, fibers, system modules, audio output, the display, the RTC library, and the sceClib string, format and memory space calls. Other areas can be added in the same format.

A test prints one line per value it observes, like `lock with a count of 0: 0x8002000F`, and ends with `DONE`. A run passes when the test exits with 0 and its output matches the `.expected` file next to its source. Tests print errors, counts and comparisons, never UIDs, addresses or times, so the output is the same on every run.

## Build

Needs vitasdk, with `VITASDK` set, and its taihen package for the USB plugins.

```
cmake -B build -G Ninja
cmake --build build
```

Every test goes into one vpk, `build/tests/vita-autotests.vpk`, with title ID `VTST00000`. The plugins to run the tests over USB go into `build/usb/`. Each launch runs one test in a fresh process: the one named by the first argument, by the `arg` of the launch URI, or by the first line of `ux0:data/vita-autotests/test.txt`. The name `list` prints every test.

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

## Run from the PC

`run_tests.py` runs the tests on a Vita connected to the PC, and compares their output with the `.expected` files, like pspautotests does with PSPLink. It needs Python 3.8 or later and, for USB, pyserial (`python -m pip install pyserial`).

Keep the Vita unlocked for the whole run. Behind the lock screen, which shows after a boot and after the Vita went to standby, some tests that run threads on several cores don't end, like `core_affinity`, `nonblocking_wait_preempt` and `callback_outer_wake_more`, and the run reports them as timed out. So unlock the Vita before a run. Over USB, the shell plugin resets the Vita's idle timers with `sceKernelPowerTick` before each test and every second while one runs, which keeps Auto Standby from starting during the run. Over Wi-Fi, set Auto Standby in the power save settings to its longest time, and if the Vita still goes to standby during a long run, unlock it and run the tests that timed out again.

### Over USB

Besides the vpk, the Vita needs two plugins, which the build puts in `build/usb/` and CI uploads with the vpk:

- `vita-autotests-usb.skprx`, a kernel plugin that frees the USB port for the USB serial port, and turns MTP off, so the Vita no longer shows up for Content Manager.
- `vita-autotests-shell.suprx`, a SceShell plugin that opens the USB serial port, launches the test app for each test it's sent, and sends back its output file.

Copy them to `ur0:tai/`, add them to `ur0:tai/config.txt`, and reboot:

```
*KERNEL
ur0:tai/vita-autotests-usb.skprx

*main
ur0:tai/vita-autotests-shell.suprx
```

Then connect the Vita by USB, on its home screen. It shows up on the PC as a serial port, which `run_tests.py` finds by itself, or pass it with `--port`:

```
python run_tests.py                     # every test
python run_tests.py mutex kernel/cond   # tests by name or path
python run_tests.py tests/io            # the tests in a directory
python run_tests.py -v mutex            # with the diff of a failing test
python run_tests.py -g mutex            # saves the output as mutex.expected
```

Each test runs in a fresh process. A test still running after `--timeout` seconds, 60 by default, is closed and fails. Starting a test closes the app open on the Vita, if any, and the Vita shouldn't be used while the tests run. Both plugins write how the link came up since the last boot to `ur0:tai/vita-autotests-kernel.log` and `ur0:tai/vita-autotests-usb.log`.

The plugins are built safe, without `UNSAFE`, since SceShell doesn't load a plugin built unsafe.

### Over Wi-Fi

`run_tests.py` can also run the tests over Wi-Fi with [vitacompanion](https://github.com/devnoname120/vitacompanion) on the Vita, instead of the plugins, given the Vita's IP address with `--host` or `VITA_HOST`:

```
python run_tests.py --host 192.168.1.20
```

For each test it writes the test name to `test.txt` over vitacompanion's FTP server, launches the app through its command port, and reads the output file until it ends with `DONE`.

## Run on an emulator

Pass the test name as the first argument, and compare the debug output with the test's `.expected` file.

## Add a test

Write `tests/<area>/<name>.c` with `PRINT`, `PRINTF` and `test_exit()` from `common/test.h`, and add it to `tests/CMakeLists.txt`. Run it on a Vita and save its output as `tests/<area>/<name>.expected`.

Cases that pass NULL pointers or huge sizes go at the end of a test, so an implementation that crashes on them loses nothing else. An info call given a size past its struct gets a buffer with room behind it, in case it writes that far.

A case that a wrong implementation may crash or hang on, like `wait_own_end`, goes in a test of its own, so it doesn't lose the output of the checks after it.

A case that needs something a Vita doesn't promise, like two memory blocks next to each other in `memblock_neighbors`, goes in a test of its own too. When that doesn't hold, the test ends with `test_skip()`, and the runner reports it as skipped instead of failed.

The output must not depend on timing, so it is the same however many times the test runs:

- A test never sleeps a fixed time to let another thread get somewhere. It waits for what that thread records, or for its end, with `TEST_WAIT_FOR()` or a join.
- A value is only read while every other thread of the test is blocked or has ended. Where the test expects a wake to do nothing, it reads right after the call that would have done it.
- Times are printed as comparisons, with every threshold far from each outcome the test tells apart. A timer event that must have fired gets at least ten times its interval.
