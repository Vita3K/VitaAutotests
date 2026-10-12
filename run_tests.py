#!/usr/bin/env python3
"""Runs the tests on a Vita on the same network, and compares their output with the .expected files.

The Vita needs the test vpk installed and vitacompanion loaded, see the README. Each test runs in a
fresh process, like launching it from the LiveArea. Needs only Python 3.8 or later.

  python run_tests.py --host 192.168.1.20       runs every test
  python run_tests.py -H 192.168.1.20 mutex kernel/cond tests/io
                                                runs tests by name, by source path or by directory
  python run_tests.py -H 192.168.1.20 -g mutex  saves the output of mutex as its .expected file

The host can also be set in the VITA_HOST environment variable.
"""

import argparse
import difflib
import ftplib
import io
import os
import re
import socket
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TESTS_DIR = ROOT / "tests"

TITLE_ID = "VTST00000"
FTP_PORT = 1337
COMMAND_PORT = 1338
DATA_DIR = "ux0:/data/vita-autotests"
SELECTION_PATH = f"{DATA_DIR}/test.txt"
OUTPUT_DIR = f"{DATA_DIR}/output"
# The last line of a test's output, which test_exit() prints
DONE = b"DONE\n"

POLL_S = 0.5
# How long the app has to show up after the launch, to close after it's told to quit, and to end
# its process after it printed DONE
START_TIMEOUT_S = 20
CLOSE_S = 2
SETTLE_S = 1


def load_tests():
    """Returns {name: path of the .expected file} for each vita_test() in tests/CMakeLists.txt."""
    tests = {}
    text = (TESTS_DIR / "CMakeLists.txt").read_text()
    for name, source in re.findall(r"^\s*vita_test\(\s*(\w+)\s+(\S+?)[\s)]", text, re.MULTILINE):
        tests[name] = TESTS_DIR / Path(source).parent / f"{name}.expected"
    return tests


def select_tests(tests, args):
    """Resolves each argument, a name, a source or .expected path, or a directory, to test names."""
    if not args:
        return list(tests)
    selected = []
    for arg in args:
        if arg in tests:
            selected.append(arg)
            continue
        path = Path(arg)
        candidates = [path] if path.is_absolute() else [path, TESTS_DIR / path]
        directory = next((p.resolve() for p in candidates if p.is_dir()), None)
        if directory is not None:
            found = [name for name, expected in tests.items() if expected.parent.resolve() == directory]
            if not found:
                sys.exit(f"no tests in {arg}")
            selected += found
        elif path.stem in tests:
            selected.append(path.stem)
        else:
            sys.exit(f"unknown test: {arg}")
    return list(dict.fromkeys(selected))


class Vita:
    """The Vita through vitacompanion: its FTP server for the files, its command port for launching."""

    def __init__(self, host):
        self.host = host
        self.ftp = None
        # Whether an app may be running, which is unknown until the first test
        self.app_maybe_open = True

    def command(self, text):
        """Sends a vitacompanion command, and returns what it answers within a moment."""
        with socket.create_connection((self.host, COMMAND_PORT), timeout=5) as sock:
            sock.sendall(text.encode() + b"\n")
            sock.settimeout(0.5)
            try:
                return sock.recv(4096).decode(errors="replace").strip()
            except socket.timeout:
                return ""

    def connect_ftp(self):
        if self.ftp is not None:
            try:
                self.ftp.quit()
            except (OSError, EOFError, ftplib.Error):
                pass
        self.ftp = ftplib.FTP()
        self.ftp.connect(self.host, FTP_PORT, timeout=10)
        self.ftp.login()
        self.ftp.voidcmd("TYPE I")

    def ftp_call(self, func):
        """Runs func(ftp), once more on a new connection if the connection was dropped."""
        for attempt in range(2):
            try:
                if self.ftp is None or attempt:
                    self.connect_ftp()
                return func(self.ftp)
            except (OSError, EOFError, ftplib.error_temp, ftplib.error_reply):
                if attempt:
                    raise

    def prepare_dirs(self):
        def make(ftp):
            for path in (DATA_DIR, OUTPUT_DIR):
                try:
                    ftp.mkd(path)
                except ftplib.error_perm:
                    pass  # it's there already

        self.ftp_call(make)

    def select(self, name):
        self.ftp_call(lambda ftp: ftp.storbinary(f"STOR {SELECTION_PATH}", io.BytesIO(name.encode() + b"\n")))

    def remove_output(self, name):
        def remove(ftp):
            try:
                ftp.delete(f"{OUTPUT_DIR}/{name}.txt")
            except ftplib.error_perm:
                pass  # no output from an earlier run

        self.ftp_call(remove)

    def read_output(self, name):
        """Returns the output file of a test so far, or None if there is none yet."""

        def read(ftp):
            data = io.BytesIO()
            try:
                ftp.retrbinary(f"RETR {OUTPUT_DIR}/{name}.txt", data.write)
            except ftplib.error_perm:
                return None
            return data.getvalue()

        return self.ftp_call(read)

    def run(self, name, timeout_s):
        """Runs a test. Returns its output and how it ended: exited, timeout or not-started."""
        # A launch would only bring back an app left from an earlier run. One that finished closes by
        # itself, so only an app of unknown state, or one that hung, is told to quit
        if self.app_maybe_open:
            self.command("quit all")
            time.sleep(CLOSE_S)
        else:
            time.sleep(SETTLE_S)
        # The app deletes its old output when it starts, so a stale one has to go first
        self.remove_output(name)
        self.select(name)
        self.command("screen on")
        self.command(f"launch {TITLE_ID}")
        self.app_maybe_open = True

        launched = time.monotonic()
        # Since the output file showed up, which is when the app is known to run
        running_since = None
        output = b""
        while True:
            now = time.monotonic()
            data = self.read_output(name)
            if data is not None:
                output = data
                if output.endswith(DONE):
                    self.app_maybe_open = False
                    return output.decode(errors="replace"), "exited"
                if running_since is None:
                    running_since = now
                elif now - running_since > timeout_s:
                    status = "timeout"
                    break
            elif running_since is None and now - launched > START_TIMEOUT_S:
                status = "not-started"
                break
            time.sleep(POLL_S)
        self.command("quit all")
        return output.decode(errors="replace"), status


def normalize(text):
    return text.replace("\r\n", "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tests", nargs="*", help="test names, source paths or directories, all tests if none")
    parser.add_argument("-H", "--host", default=os.environ.get("VITA_HOST"),
                        help="IP address of the Vita, or the VITA_HOST environment variable")
    parser.add_argument("-t", "--timeout", type=float, default=60, help="seconds a test may run, 60 by default")
    parser.add_argument("-g", "--generate", action="store_true", help="save the output as the .expected files")
    parser.add_argument("-v", "--verbose", action="store_true", help="show the diff of each failing test")
    args = parser.parse_args()
    if not args.host:
        sys.exit("pass the IP address of the Vita with --host, or set VITA_HOST")

    tests = load_tests()
    names = select_tests(tests, args.tests)
    vita = Vita(args.host)
    try:
        print(f"vitacompanion: {vita.command('version') or 'no answer to version'}")
        vita.command("nosleep on")
        vita.prepare_dirs()
    except OSError as error:
        sys.exit(f"can't reach vitacompanion on {args.host}: {error}\n"
                 "Is the Vita on the same network, with vitacompanion loaded?")

    failed = []
    for name in names:
        expected_path = tests[name]
        try:
            output, status = vita.run(name, args.timeout)
        except OSError as error:
            sys.exit(f"{name}: lost the Vita: {error}")
        relative = expected_path.relative_to(ROOT).with_suffix("").as_posix()

        if args.generate:
            if status == "exited" and output:
                expected_path.write_text(normalize(output))
                print(f"SAVED {relative}")
            else:
                failed.append(name)
                print(f"FAIL  {relative} ({status}), not saved")
            continue

        expected = normalize(expected_path.read_text()) if expected_path.exists() else None
        if status == "exited" and expected is not None and normalize(output) == expected:
            print(f"PASS  {relative}")
            continue
        failed.append(name)
        reason = status if status != "exited" else "no .expected file" if expected is None else "output differs"
        print(f"FAIL  {relative} ({reason})")
        if args.verbose:
            diff = difflib.unified_diff((expected or "").splitlines(), normalize(output).splitlines(),
                                        "expected", "vita", lineterm="")
            for line in diff:
                print("      " + line)

    print(f"{len(names) - len(failed)}/{len(names)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
