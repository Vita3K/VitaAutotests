#!/usr/bin/env python3
"""Runs the tests on a Vita connected to the PC, and compares their output with the .expected files.

The Vita needs the test vpk installed, and is reached over USB with the plugins from usb/, or over
Wi-Fi with vitacompanion, see the README. Each test runs in a fresh process, like launching it from
the LiveArea. Needs Python 3.8 or later, and pyserial for USB.

The tests can also run on an emulator, given the command that runs one test and its ux0: folder.
The captures a render test makes are compared with the images a Vita gave, next to its .expected
file, within the tolerances in tests/tolerances.txt.

  python run_tests.py                       runs every test over USB
  python run_tests.py mutex kernel/cond tests/io
                                            runs tests by name, by source path or by directory
  python run_tests.py -g mutex              saves the output of mutex as its .expected file
  python run_tests.py -H 192.168.1.20       runs every test over Wi-Fi with vitacompanion
  python run_tests.py -E "Vita3K --frontend sdl -r VTST00000 -Z {test}" --ux0 path/to/ux0 tests/render
                                            runs the render tests on Vita3K

The host can also be set in the VITA_HOST environment variable.
"""

import argparse
import difflib
import ftplib
import io
import os
import re
import shlex
import socket
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
TESTS_DIR = ROOT / "tests"

TITLE_ID = "VTST00000"
SONY_VID = 0x054C
USB_PROTOCOL = "vita-autotests usb 2"
# How long the shell plugin may take to start the app and to close it, on top of the test timeout
USB_LAUNCH_MARGIN_S = 30
FTP_PORT = 1337
COMMAND_PORT = 1338
DATA_DIR = "ux0:/data/vita-autotests"
SELECTION_PATH = f"{DATA_DIR}/test.txt"
OUTPUT_DIR = f"{DATA_DIR}/output"
# The last line of a test's output, which test_exit() prints
DONE = b"DONE\n"
# The line test_capture() prints for each capture, saved as <name>.png in the test's directory
CAPTURE_LINE = re.compile(r"^capture (\S+): 0x([0-9A-F]{8})$", re.MULTILINE)
TOLERANCES_PATH = TESTS_DIR / "tolerances.txt"

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


def find_usb_port():
    """Returns the serial port of the Vita, which the plugins from usb/ open."""
    import serial.tools.list_ports

    ports = [p for p in serial.tools.list_ports.comports() if p.vid == SONY_VID]
    if len(ports) == 1:
        return ports[0].device
    if not ports:
        others = ", ".join(p.device for p in serial.tools.list_ports.comports()) or "none"
        sys.exit(f"no Vita serial port found (ports: {others}). Is it connected by USB, with the plugins "
                 "from usb/ loaded? Or pass --port, or --host to run over Wi-Fi")
    sys.exit("several Vita serial ports: " + ", ".join(p.device for p in ports) + ", pass one with --port")


class UsbVita:
    """The Vita through the plugins from usb/, which run each test and send back its output."""

    def __init__(self, port):
        import serial

        self.port = port
        self.serial = serial.Serial(port, 115200, timeout=1)

    def start(self):
        """Checks that the shell plugin answers, and returns where it is."""
        self.serial.reset_input_buffer()
        self.serial.write(b"\nhello\n")
        deadline = time.monotonic() + 10
        while not (line := self.read_line(deadline)).startswith("vita-autotests usb "):
            pass
        if line != USB_PROTOCOL:
            sys.exit(f"the shell plugin on the Vita speaks {line}, not {USB_PROTOCOL}. Copy the one from this "
                     "build to ur0:tai/ and reboot")
        return f"{USB_PROTOCOL} on {self.port}"

    def read_line(self, deadline):
        line = b""
        while not line.endswith(b"\n"):
            if time.monotonic() > deadline:
                raise TimeoutError("the Vita stopped answering")
            line += self.serial.read_until(b"\n")
        return line.decode(errors="replace").strip()

    def read_exactly(self, size, deadline):
        data = b""
        while len(data) < size:
            if time.monotonic() > deadline:
                raise TimeoutError("the Vita stopped answering")
            data += self.serial.read(size - len(data))
        return data

    def run(self, name, timeout_s):
        """Runs a test. Returns its output, how it ended: exited, timeout or not-started, and the files
        in its capture directory."""
        self.serial.write(f"run {name} {int(timeout_s * 1000)}\n".encode())
        deadline = time.monotonic() + timeout_s + USB_LAUNCH_MARGIN_S
        while not (line := self.read_line(deadline)).startswith("output "):
            pass
        output = self.read_exactly(int(line.split()[1]), deadline)
        files = {}
        while not (line := self.read_line(deadline)).startswith("end "):
            if line.startswith("file "):
                file_name, size = line[len("file "):].rsplit(" ", 1)
                files[file_name] = self.read_exactly(int(size), deadline)
        return output.decode(errors="replace"), line.split(maxsplit=1)[1], files


class CompanionVita:
    """The Vita through vitacompanion: its FTP server for the files, its command port for launching."""

    def __init__(self, host):
        self.host = host
        self.ftp = None
        # Whether an app may be running, which is unknown until the first test
        self.app_maybe_open = True

    def start(self):
        """Checks that vitacompanion answers, and returns what it says it is."""
        version = self.command("version") or "no answer to version"
        self.command("nosleep on")
        self.prepare_dirs()
        return f"{version} on {self.host}"

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

    def read_file(self, path):
        """Returns the file at path, or None if there is none."""

        def read(ftp):
            data = io.BytesIO()
            try:
                ftp.retrbinary(f"RETR {path}", data.write)
            except ftplib.error_perm:
                return None
            return data.getvalue()

        return self.ftp_call(read)

    def read_output(self, name):
        """Returns the output file of a test so far, or None if there is none yet."""
        return self.read_file(f"{OUTPUT_DIR}/{name}.txt")

    def read_captures(self, name, output):
        """Returns the captures the output says the test made."""
        files = {}
        for capture, _ in CAPTURE_LINE.findall(output):
            data = self.read_file(f"{OUTPUT_DIR}/{name}/{capture}.png")
            if data is not None:
                files[f"{capture}.png"] = data
        return files

    def run(self, name, timeout_s):
        """Runs a test. Returns its output, how it ended: exited, timeout or not-started, and its captures."""
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
                    text = output.decode(errors="replace")
                    return text, "exited", self.read_captures(name, text)
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
        text = output.decode(errors="replace")
        return text, status, self.read_captures(name, text)


class EmulatorVita:
    """An emulator, run once for each test with a command, which writes the output to its ux0: folder."""

    def __init__(self, command, ux0):
        self.command = command
        self.ux0 = Path(ux0)
        self.data_dir = self.ux0 / "data" / "vita-autotests"

    def start(self):
        if not self.ux0.is_dir():
            sys.exit(f"no ux0: folder at {self.ux0}")
        return f"emulator: {self.command}"

    def run(self, name, timeout_s):
        """Runs a test. Returns its output, how it ended: exited, timeout or not-started, and its captures."""
        output_dir = self.data_dir / "output"
        output_dir.mkdir(parents=True, exist_ok=True)
        # For a command without {test}, the app reads the name from test.txt
        (self.data_dir / "test.txt").write_text(name + "\n")
        output_path = output_dir / f"{name}.txt"
        if output_path.exists():
            output_path.unlink()
        capture_dir = output_dir / name
        for stale in capture_dir.glob("*") if capture_dir.is_dir() else []:
            stale.unlink()

        command = self.command.replace("{test}", name)
        # Windows splits the command line itself, and shlex would take its backslashes as escapes
        process = subprocess.Popen(command if os.name == "nt" else shlex.split(command),
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            process.wait(timeout_s)
            status = "exited"
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            status = "timeout"
        if not output_path.exists():
            return "", "not-started" if status == "exited" else status, {}
        files = {path.name: path.read_bytes() for path in capture_dir.glob("*.png")} if capture_dir.is_dir() else {}
        return output_path.read_bytes().decode(errors="replace"), status, files


def read_png(data):
    """Returns the width, height and RGBA pixels of an 8-bit RGB or RGBA PNG."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos, idat, header = 8, [], None
    while pos < len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if header is None:
        raise ValueError("no IHDR chunk")
    width, height, depth, color_type, _, _, interlace = header
    if depth != 8 or color_type not in (2, 6) or interlace:
        raise ValueError("only 8-bit RGB and RGBA PNG files without interlacing are read")
    channels = 4 if color_type == 6 else 3
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows, prev = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, row = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if kind == 2:
            row = bytearray((a + b) & 0xFF for a, b in zip(row, prev))
        elif kind != 0:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                up = prev[i]
                if kind == 1:
                    row[i] = (row[i] + left) & 0xFF
                elif kind == 3:
                    row[i] = (row[i] + ((left + up) >> 1)) & 0xFF
                elif kind == 4:
                    up_left = prev[i - channels] if i >= channels else 0
                    p = left + up - up_left
                    pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                    row[i] = (row[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else up_left)) & 0xFF
        rows.append(bytes(row))
        prev = row
    pixels = b"".join(rows)
    if channels == 3:
        rgba = bytearray(width * height * 4)
        rgba[0::4], rgba[1::4], rgba[2::4] = pixels[0::3], pixels[1::3], pixels[2::3]
        rgba[3::4] = b"\xff" * (width * height)
        pixels = bytes(rgba)
    return width, height, pixels


def write_png(path, width, height, pixels):
    """Writes RGBA pixels as a PNG file."""
    stride = width * 4
    raw = b"".join(b"\0" + pixels[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def load_tolerances():
    """Returns {test or test.capture: (largest channel difference, how many pixels may go past it)}."""
    tolerances = {}
    if TOLERANCES_PATH.exists():
        for line in TOLERANCES_PATH.read_text().splitlines():
            fields = line.split("#", 1)[0].split()
            if fields:
                tolerances[fields[0]] = (int(fields[1]), int(fields[2]))
    return tolerances


def load_list(path):
    """Returns the test names in a file of one name per line, with # comments."""
    if path is None:
        return set()
    return {line.split("#", 1)[0].strip() for line in Path(path).read_text().splitlines()} - {""}


def compare_images(actual, reference, tolerance, diff_path):
    """Compares two PNG files within a tolerance. Returns None when they match, or why not, after
    writing an image of the pixels that differ to diff_path."""
    try:
        width, height, got = read_png(actual)
        ref_width, ref_height, want = read_png(reference)
    except (ValueError, zlib.error, struct.error) as error:
        return f"can't read it: {error}"
    if (width, height) != (ref_width, ref_height):
        return f"it is {width}x{height}, not {ref_width}x{ref_height}"
    if got == want:
        return None
    max_diff, max_pixels = tolerance
    over, largest = 0, 0
    diff = bytearray(len(want))
    for i in range(0, len(want), 4):
        d = max(abs(got[i] - want[i]), abs(got[i + 1] - want[i + 1]), abs(got[i + 2] - want[i + 2]),
                abs(got[i + 3] - want[i + 3]))
        if d > largest:
            largest = d
        if d > max_diff:
            over += 1
            diff[i:i + 4] = b"\xff\x00\xff\xff"
        else:
            # What it should be, dimmed, so the pixels that differ stand out
            gray = (want[i] + want[i + 1] + want[i + 2]) // 9
            diff[i:i + 4] = bytes((gray, gray, gray, 255))
    if over <= max_pixels:
        return None
    diff_path.parent.mkdir(parents=True, exist_ok=True)
    write_png(diff_path, width, height, bytes(diff))
    return f"{over} pixels differ by more than {max_diff}, by up to {largest}"


def normalize(text):
    return text.replace("\r\n", "\n")


def capture_names(output):
    return [capture for capture, _ in CAPTURE_LINE.findall(output)]


def compare(name, output, status, files, expected, image_path, tolerances, diff_dir, what):
    """Compares a run with what it should give: expected, the output, and image_path(capture), the
    image of each capture. Returns why it differs, nothing when it matches. what names where the
    expected output came from."""
    if status != "exited":
        return [status]
    reasons = []
    if expected is None:
        reasons.append(f"no output from {what}")
    elif normalize(output) != expected:
        reasons.append(f"output differs from {what}")
    for capture in capture_names(output):
        data = files.get(f"{capture}.png")
        reference = image_path(capture)
        if data is None:
            reasons.append(f"capture {capture} is missing")
        elif not reference.exists():
            reasons.append(f"no image of capture {capture} from {what}")
        else:
            tolerance = tolerances.get(f"{name}.{capture}", tolerances.get(name, (0, 0)))
            diff_path = diff_dir / f"{name}.{capture}.{'golden-' if what == 'golden' else ''}diff.png"
            why = compare_images(data, reference.read_bytes(), tolerance, diff_path)
            if why:
                reasons.append(f"capture {capture} differs from {what}: {why}")
    return reasons


def save_run(name, output, files, text_path, image_path):
    """Saves the output of a run to text_path and each of its captures to image_path(capture).
    Returns the captures that are missing."""
    missing = [capture for capture in capture_names(output) if f"{capture}.png" not in files]
    if not missing:
        text_path.parent.mkdir(parents=True, exist_ok=True)
        text_path.write_text(normalize(output))
        for capture in capture_names(output):
            image_path(capture).write_bytes(files[f"{capture}.png"])
    return missing


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tests", nargs="*", help="test names, source paths or directories, all tests if none")
    parser.add_argument("-p", "--port", help="serial port of the Vita, like COM5, found by itself if not given")
    parser.add_argument("-H", "--host", default=os.environ.get("VITA_HOST"),
                        help="IP address of the Vita, to run over Wi-Fi with vitacompanion instead of USB, "
                             "or the VITA_HOST environment variable")
    parser.add_argument("-E", "--emulator",
                        help="command that runs one test on an emulator, with {test} where the test name goes")
    parser.add_argument("--ux0", help="the ux0: folder of the emulator, where the tests write their output")
    parser.add_argument("-t", "--timeout", type=float, default=60, help="seconds a test may run, 60 by default")
    parser.add_argument("-g", "--generate", action="store_true",
                        help="save the output as the .expected files, and the captures as the images next to them")
    parser.add_argument("-v", "--verbose", action="store_true", help="show the diff of each failing test")
    parser.add_argument("--captures", type=Path, default=ROOT / "build" / "captures",
                        help="folder for the captures and the images of the pixels that differ, "
                             "build/captures by default")
    parser.add_argument("--golden", type=Path,
                        help="folder of the output of an earlier run, to also compare with, which shows what "
                             "changed since")
    parser.add_argument("--update-golden", action="store_true", help="save the output to the --golden folder")
    parser.add_argument("--known-failures",
                        help="file of the tests known to differ from a Vita, one per line, which don't fail the "
                             "run unless they differ from --golden too")
    args = parser.parse_args()
    if args.emulator and not args.ux0:
        parser.error("--emulator needs --ux0")
    if args.emulator and args.generate:
        parser.error("-g saves what a Vita gives, use --golden and --update-golden for an emulator")
    if args.update_golden and not args.golden:
        parser.error("--update-golden needs --golden")

    tests = load_tests()
    names = select_tests(tests, args.tests)
    tolerances = load_tolerances()
    known_failures = load_list(args.known_failures)
    try:
        if args.emulator:
            vita = EmulatorVita(args.emulator, args.ux0)
        elif args.host:
            vita = CompanionVita(args.host)
        else:
            try:
                import serial  # noqa: F401
            except ImportError:
                sys.exit("running over USB needs pyserial: python -m pip install pyserial")
            vita = UsbVita(args.port or find_usb_port())
        print(vita.start())
    except OSError as error:
        where = (f"vitacompanion on {args.host}. Is the Vita on the same network, with vitacompanion loaded?"
                 if args.host else "the Vita over USB. Are the plugins from usb/ loaded?")
        sys.exit(f"can't reach {where}\n{error}")
    args.captures.mkdir(parents=True, exist_ok=True)

    failed, skipped, known = [], [], []
    for name in names:
        expected_path = tests[name]
        try:
            output, status, files = vita.run(name, args.timeout)
        except OSError as error:
            sys.exit(f"{name}: lost the {'emulator' if args.emulator else 'Vita'}: {error}")
        relative = expected_path.relative_to(ROOT).with_suffix("").as_posix()
        for file_name, data in files.items():
            (args.captures / f"{name}.{file_name}").write_bytes(data)

        def vita_image(capture):
            return expected_path.with_name(f"{name}.{capture}.png")

        def golden_image(capture):
            return args.golden / f"{name}.{capture}.png"

        # A test whose precondition didn't hold ends with test_skip(), which prints its reason and DONE
        lines = normalize(output).splitlines()
        if status == "exited" and len(lines) == 2 and lines[0].startswith("SKIP: ") and lines[1] == "DONE":
            skipped.append(name)
            print(f"SKIP  {relative} ({lines[0][len('SKIP: '):]})")
            continue

        if args.generate:
            missing = save_run(name, output, files, expected_path, vita_image) if status == "exited" and output else []
            if status == "exited" and output and not missing:
                print(f"SAVED {relative}")
            else:
                failed.append(name)
                reason = status if status != "exited" or not output else "missing capture " + ", ".join(missing)
                print(f"FAIL  {relative} ({reason}), not saved")
            continue

        expected = normalize(expected_path.read_text()) if expected_path.exists() else None
        reasons = compare(name, output, status, files, expected, vita_image, tolerances, args.captures, "the Vita")
        changes = []
        if args.update_golden:
            if status == "exited" and output:
                save_run(name, output, files, args.golden / f"{name}.txt", golden_image)
        elif args.golden:
            golden_path = args.golden / f"{name}.txt"
            golden = normalize(golden_path.read_text()) if golden_path.exists() else None
            changes = compare(name, output, status, files, golden, golden_image, tolerances, args.captures, "golden")

        if name in known_failures:
            label = "FIXED" if not reasons else "KNOWN"
        else:
            label = "PASS" if not reasons else "FAIL"
        if changes:
            label = "FAIL"
        if label == "FAIL":
            failed.append(name)
        elif label == "KNOWN":
            known.append(name)
        details = reasons + changes
        if label == "FIXED":
            details = ["matches the Vita now, take it off the known failures"]
        print(f"{label:<5} {relative}" + (f" ({'; '.join(details)})" if details else ""))
        if args.verbose and label in ("FAIL", "KNOWN"):
            if expected is not None and status == "exited" and normalize(output) != expected:
                diff = difflib.unified_diff(expected.splitlines(), normalize(output).splitlines(),
                                            "expected", "got", lineterm="")
                for line in diff:
                    print("      " + line)
            for capture in capture_names(output):
                for diff_path in sorted(args.captures.glob(f"{name}.{capture}.*diff.png")):
                    print(f"      {diff_path}")

    summary = f"{len(names) - len(failed) - len(known) - len(skipped)}/{len(names)} passed, {len(skipped)} skipped"
    if known:
        summary += f", {len(known)} known to fail"
    print(summary)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
