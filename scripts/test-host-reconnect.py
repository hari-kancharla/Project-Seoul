#!/usr/bin/env python3
"""Integration tests for SeoulHost's socket resilience.

Real processes, real sockets, real timing. What is under test is the host's
behaviour when SeoulApp is absent, late, or killed mid-session — which is a
property of process lifetimes and cannot be checked by a unit test.

WHY A STAND-IN FOR SeoulApp

SeoulApp is a menu bar app: an NSStatusItem, a Carbon hotkey on cmd+shift+space,
and a window server connection. Starting and killing it a dozen times inside a
test would need a GUI session, would fight the user's own SeoulApp for
/tmp/seoul.sock, and would grab their hotkey while it ran. None of that would
test SeoulHost any harder, because the host never parses what comes back — it
is a byte relay, and it cannot tell a real SeoulApp from anything else speaking
the same framing on the same socket. So the listener here is forty lines of
Python and the socket path comes from SEOUL_SOCKET_PATH, well away from a
SeoulApp the user has running for real.

The parts of SeoulApp that are real logic — the stale-socket unlink, the
deferred hotkey queue — are tested against the actual Swift types in
SeoulTests/UnixSocketServerTests.swift and SeoulTests/QueueTests.swift.

Usage:  swift build && python3 scripts/test-host-reconnect.py
"""

import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST_BIN = os.path.join(REPO, ".build", "debug", "SeoulHost")
APP_BIN = os.path.join(REPO, ".build", "debug", "SeoulApp")

# Native byte order, matching NativeMessaging.frame. "=" is standard size and
# native order; "!" here would be the classic bug that makes a 27-byte payload
# announce itself as 452,984,832 bytes.
LENGTH = struct.Struct("=I")


def frame(payload: bytes) -> bytes:
    return LENGTH.pack(len(payload)) + payload


# ---------------------------------------------------------------------------
# The stand-in for SeoulApp
# ---------------------------------------------------------------------------

class FakeApp:
    """Binds the socket and speaks native-messaging framing. Nothing else."""

    def __init__(self, path):
        self.path = path
        self.received = []          # payloads, in arrival order
        self.connections = 0        # how many hosts have connected, ever
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._server = None
        self._conn = None
        self._thread = None

    def start(self):
        # The same rule SeoulApp follows in UnixSocketServer.start(): a stale
        # socket file survives a crash and makes bind() fail forever after.
        self._unlink()
        self._server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._server.bind(self.path)
        self._server.listen(4)
        self._server.settimeout(0.1)
        self._stop.clear()
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()

    def stop(self):
        """What killing SeoulApp looks like from the host's side."""
        self._stop.set()
        with self._lock:
            conn, self._conn = self._conn, None
        for sock in (conn, self._server):
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
        self._server = None
        if self._thread is not None:
            self._thread.join(timeout=2)
            self._thread = None
        self._unlink()

    def send(self, payload: bytes) -> bool:
        """SeoulApp -> host -> Chrome."""
        with self._lock:
            conn = self._conn
        if conn is None:
            return False
        conn.sendall(frame(payload))
        return True

    def snapshot(self):
        with self._lock:
            return list(self.received), self.connections

    def _unlink(self):
        try:
            os.unlink(self.path)
        except FileNotFoundError:
            pass

    def _accept_loop(self):
        while not self._stop.is_set():
            try:
                conn, _ = self._server.accept()
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                return
            with self._lock:
                self.connections += 1
                self._conn = conn
            self._read_loop(conn)

    def _read_loop(self, conn):
        conn.settimeout(0.1)
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = conn.recv(65536)
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while len(buf) >= LENGTH.size:
                (n,) = LENGTH.unpack(buf[:LENGTH.size])
                if len(buf) < LENGTH.size + n:
                    break
                with self._lock:
                    self.received.append(buf[LENGTH.size:LENGTH.size + n])
                buf = buf[LENGTH.size + n:]
        try:
            conn.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# The process under test
# ---------------------------------------------------------------------------

class Host:
    """One SeoulHost process, with its stdin, stdout and stderr in hand."""

    def __init__(self, socket_path, workdir, name):
        self.stderr_path = os.path.join(workdir, f"{name}.stderr")
        self._stderr_file = open(self.stderr_path, "wb")
        env = dict(os.environ, SEOUL_SOCKET_PATH=socket_path)
        self.proc = subprocess.Popen(
            [HOST_BIN],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=self._stderr_file, env=env,
        )
        self.pid = self.proc.pid
        self.frames_out = []        # frames the host wrote to stdout
        self._lock = threading.Lock()
        self._reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._reader.start()

    def _read_stdout(self):
        buf = b""
        while True:
            chunk = self.proc.stdout.read(1)
            if not chunk:
                return
            buf += chunk
            while len(buf) >= LENGTH.size:
                (n,) = LENGTH.unpack(buf[:LENGTH.size])
                if len(buf) < LENGTH.size + n:
                    break
                with self._lock:
                    self.frames_out.append(buf[LENGTH.size:LENGTH.size + n])
                buf = buf[LENGTH.size + n:]

    def stdout_frames(self):
        with self._lock:
            return list(self.frames_out)

    def send(self, payload: bytes):
        """Chrome -> host."""
        self.proc.stdin.write(frame(payload))
        self.proc.stdin.flush()

    def close_stdin(self):
        """Chrome closing the port."""
        self.proc.stdin.close()

    def alive(self):
        return self.proc.poll() is None

    def stderr(self):
        self._stderr_file.flush()
        with open(self.stderr_path, "r", errors="replace") as handle:
            return handle.read()

    def kill(self):
        if self.alive():
            self.proc.kill()
            self.proc.wait(timeout=5)
        self._stderr_file.close()


# ---------------------------------------------------------------------------
# Assertions and waiting
# ---------------------------------------------------------------------------

class Failure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failure(message)
    print(f"      ok   {message}")


def wait_until(predicate, timeout, what):
    """Polls instead of sleeping a fixed amount: a test that passes because a
    sleep was generous is a test that fails on a loaded machine."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.02)
    raise Failure(f"timed out after {timeout:.1f}s waiting for {what}")


def count_lines(text, needle):
    return sum(1 for line in text.splitlines() if needle in line)


# ---------------------------------------------------------------------------
# The tests
# ---------------------------------------------------------------------------

def test_1_starts_without_seoulapp(sock, work):
    """Host starts with no SeoulApp: stays up, retries, connects when the app
    finally arrives — without being restarted."""
    host = Host(sock, work, "t1")
    app = FakeApp(sock)
    try:
        # Ten seconds of no SeoulApp at all. Before this change the process was
        # gone one second in, with exit 1.
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if not host.alive():
                raise Failure(
                    f"host exited after {10 - (deadline - time.monotonic()):.1f}s "
                    f"with code {host.proc.returncode}"
                )
            time.sleep(0.25)

        check(host.alive(), "host is still running after 10s with no SeoulApp")

        log = host.stderr()
        attempts = count_lines(log, "connect attempt")
        # 100ms doubling to a 2s cap reaches the cap after ~4s, so ten seconds
        # is at least 5 backoff steps plus ~3 at the cap.
        check(attempts >= 8, f"logged {attempts} connect attempts while waiting")
        check("failed:" in log and "retrying in" in log,
              "each attempt logs the reason and the next delay")
        check("Connection refused" in log or "No such file" in log,
              "the underlying errno is in the log, not just 'failed'")
        check(host.stdout_frames() == [],
              "nothing was written to stdout (it is the protocol channel)")

        # SeoulApp turns up.
        pid_before = host.pid
        app.start()
        wait_until(lambda: "CONNECTED" in host.stderr(), 4, "the host to connect")

        check(host.alive() and host.pid == pid_before,
              f"the SAME host process (pid {pid_before}) connected — no restart")
        check(app.snapshot()[1] == 1, "SeoulApp saw exactly one connection")
        check("connection #1" in host.stderr(), "the connection is logged")
    finally:
        app.stop()
        host.kill()


def test_2_survives_seoulapp_dying_mid_session(sock, work):
    """SeoulApp is killed with the host running: the host survives, reconnects
    when it returns, and Chrome's port stays open the whole time."""
    app = FakeApp(sock)
    app.start()
    host = Host(sock, work, "t2")
    try:
        wait_until(lambda: "CONNECTED" in host.stderr(), 5, "the first connection")
        pid = host.pid

        # Prove the port works before the kill, so "it still works after" means
        # something.
        host.send(b'{"before":1}')
        wait_until(lambda: b'{"before":1}' in app.snapshot()[0], 3, "the first message")

        app.stop()          # <- SeoulApp dies
        wait_until(lambda: "SeoulApp closed the socket" in host.stderr()
                   or "link to SeoulApp is down" in host.stderr(),
                   3, "the host to notice the drop")
        time.sleep(1.0)     # long enough for a process that was going to die to die
        check(host.alive(), "host survived SeoulApp being killed")
        check(host.proc.returncode is None, "host has not exited")

        # Chrome's port is untouched: stdin still accepts frames while there is
        # nothing to relay them to.
        host.send(b'{"during":1}')
        check(host.alive(), "writing to the host while it is disconnected is fine")

        app.start()         # <- SeoulApp comes back
        wait_until(lambda: "connection #2" in host.stderr(), 6, "the reconnect")
        check(host.alive() and host.pid == pid,
              f"the SAME host process (pid {pid}) reconnected")

        # Waited on by payload, never by count: `received` carries over from
        # before the kill, so a length check would be satisfied by the wrong
        # message and pass before the one under test had arrived.
        wait_until(lambda: b'{"during":1}' in app.snapshot()[0], 5, "the held message")
        check(True, "the message sent WHILE disconnected was held and delivered")

        # The port was never closed, so this arrives on the new connection.
        host.send(b'{"after":1}')
        wait_until(lambda: b'{"after":1}' in app.snapshot()[0], 5,
                   "a message sent after the reconnect")
        check(True, "a message sent after the reconnect arrives")
        check(app.snapshot()[1] == 2,
              "two connections total: the original host, then the same host again")

        # And the reverse direction still reaches Chrome.
        app.send(b'{"reply":1}')
        wait_until(lambda: b'{"reply":1}' in host.stdout_frames(), 3,
                   "a reply to reach stdout after the reconnect")
        check(True, "SeoulApp -> Chrome still works after the reconnect")
    finally:
        app.stop()
        host.kill()


def test_3_stdin_eof_exits_zero(sock, work):
    """The one exit path. Both with and without SeoulApp present, because the
    original bug was that the no-SeoulApp case exited 1 on its own."""
    for label, with_app in (("with SeoulApp listening", True),
                            ("with no SeoulApp at all", False)):
        app = FakeApp(sock)
        if with_app:
            app.start()
        host = Host(sock, work, f"t3-{'app' if with_app else 'noapp'}")
        try:
            if with_app:
                wait_until(lambda: "CONNECTED" in host.stderr(), 5, "the connection")
            else:
                wait_until(lambda: "connect attempt" in host.stderr(), 3, "a retry")

            host.close_stdin()
            code = host.proc.wait(timeout=5)

            check(code == 0, f"exit code is 0 ({label})")
            log = host.stderr()
            check("EXIT 0" in log, f"the exit is logged ({label})")
            check("Chrome closed the port" in log,
                  f"the log says WHY it exited ({label})")
        finally:
            app.stop()
            host.kill()


def test_4_message_sent_while_disconnected_is_delivered(sock, work):
    """A frame that arrives with no socket to write it to is held, not dropped."""
    host = Host(sock, work, "t4")
    app = FakeApp(sock)
    try:
        wait_until(lambda: "connect attempt" in host.stderr(), 3, "the retry loop")
        check(host.alive(), "host is up and retrying with no SeoulApp")

        payload = b'{"op":"find","requestId":"req-1","query":"the email field"}'
        host.send(payload)
        wait_until(lambda: "buffered" in host.stderr(), 3, "the frame to be buffered")
        check(host.alive(), "the host is still alive holding an undeliverable frame")

        app.start()
        received = wait_until(lambda: app.snapshot()[0] or None, 5, "the buffered frame")
        check(received[0] == payload,
              "the frame sent while disconnected arrived intact after connecting")
        check("delivering 1 frame(s) buffered" in host.stderr(),
              "the host logs that it flushed the buffer")
    finally:
        app.stop()
        host.kill()


def test_5_buffer_overflow_drops_oldest_and_survives(sock, work):
    """32 deep, then drop the OLDEST — and a full buffer must not be fatal."""
    host = Host(sock, work, "t5")
    app = FakeApp(sock)
    try:
        wait_until(lambda: "connect attempt" in host.stderr(), 3, "the retry loop")

        total = 40
        for i in range(total):
            host.send(f'{{"n":{i}}}'.encode())
        wait_until(lambda: "outbound queue is full" in host.stderr(), 3, "the cap")
        check(host.alive(), "a full buffer did not kill the host")

        app.start()
        received = wait_until(
            lambda: app.snapshot()[0] if len(app.snapshot()[0]) >= 32 else None,
            6, "the buffer to flush")
        check(len(received) == 32, f"exactly 32 of the {total} frames were kept")
        check(received[0] == b'{"n":8}',
              "the OLDEST 8 were dropped; the newest 32 survived")
        check(received[-1] == f'{{"n":{total - 1}}}'.encode(),
              "the most recent frame is the last one delivered")
    finally:
        app.stop()
        host.kill()


def test_6_survives_repeated_seoulapp_restarts(sock, work):
    """The MV3 reality: SeoulApp cycling while one host stays put."""
    app = FakeApp(sock)
    app.start()
    host = Host(sock, work, "t6")
    try:
        wait_until(lambda: "CONNECTED" in host.stderr(), 5, "the first connection")
        pid = host.pid

        for cycle in range(2, 6):
            app.stop()
            time.sleep(0.15)
            app.start()
            wait_until(lambda: f"connection #{cycle}" in host.stderr(), 8,
                       f"reconnect #{cycle}")
            if not host.alive():
                raise Failure(f"host died during restart cycle {cycle}")

        check(host.alive() and host.pid == pid,
              f"one host process (pid {pid}) rode out 4 SeoulApp restarts")

        host.send(b'{"final":1}')
        wait_until(lambda: b'{"final":1}' in app.snapshot()[0], 4,
                   "a message on the fifth connection")
        check(True, "the bridge still relays after four reconnects")
    finally:
        app.stop()
        host.kill()


# ---------------------------------------------------------------------------
# SeoulApp's side: a hotkey pressed while no host is connected
# ---------------------------------------------------------------------------

class RealApp:
    """The actual SeoulApp binary, on a private socket.

    Runs for a few seconds and registers cmd+shift+space while it does, so it
    is used only for the two tests that genuinely need the real app: the ones
    about a hotkey fired into a gap in the bridge. Everything about the HOST is
    tested against FakeApp, which needs no window server and no hotkey.
    """

    def __init__(self, socket_path, workdir, name, args=()):
        self.stderr_path = os.path.join(workdir, f"{name}.stderr")
        self._file = open(self.stderr_path, "wb")
        env = dict(os.environ, SEOUL_SOCKET_PATH=socket_path)
        self.proc = subprocess.Popen(
            [APP_BIN, *args], stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=self._file, env=env,
        )

    def stderr(self):
        self._file.flush()
        with open(self.stderr_path, "r", errors="replace") as handle:
            return handle.read()

    def alive(self):
        return self.proc.poll() is None

    def stop(self):
        if self.alive():
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self._file.close()


class FakeHost:
    """A client on the socket, standing in for SeoulHost. SeoulApp cannot tell
    the difference — from its side a host is whatever connected last."""

    def __init__(self, path):
        self.path = path
        self.received = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._sock = None

    def connect(self, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                sock.connect(self.path)
            except OSError:
                sock.close()
                time.sleep(0.05)
                continue
            self._sock = sock
            threading.Thread(target=self._read_loop, daemon=True).start()
            return True
        return False

    def snapshot(self):
        with self._lock:
            return list(self.received)

    def _read_loop(self):
        self._sock.settimeout(0.1)
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self._sock.recv(65536)
            except (socket.timeout, TimeoutError):
                continue
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while len(buf) >= LENGTH.size:
                (n,) = LENGTH.unpack(buf[:LENGTH.size])
                if len(buf) < LENGTH.size + n:
                    break
                with self._lock:
                    self.received.append(buf[LENGTH.size:LENGTH.size + n])
                buf = buf[LENGTH.size + n:]

    def close(self):
        self._stop.set()
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass


def test_7_hotkey_with_no_host_is_queued_not_dropped(sock, work):
    """M4: a hotkey fired with nothing connected waits for a host and is sent
    the moment one arrives. The logs in the report showed it dropped instead."""
    app = RealApp(sock, work, "t7", args=["--selftest-queue"])
    host = FakeHost(sock)
    try:
        wait_until(lambda: "holding [req-1]" in app.stderr(), 5,
                   "the request to be queued")
        check("no SeoulHost connected" in app.stderr(),
              "the hotkey fired with nothing connected and was HELD, not dropped")

        check(host.connect(), "a host connects a moment later")
        request = wait_until(lambda: host.snapshot() or None, 4,
                             "the queued request to be sent")

        check(b'"requestId":"req-1"' in request[0],
              "the queued request was delivered once a host connected")
        check(b'"op":"find"' in request[0], "it is a well-formed find request")
        check("sending queued [req-1]" in app.stderr(),
              "SeoulApp logs the flush, with how long it waited")
    finally:
        host.close()
        app.stop()


def test_8_queued_hotkey_expires_after_the_window(sock, work):
    """The other half of the same rule: it waits ~2s, not forever. A sketch for
    a keypress from a minute ago points at whatever page is in front now."""
    app = RealApp(sock, work, "t8", args=["--selftest-queue"])
    try:
        wait_until(lambda: "holding [req-1]" in app.stderr(), 5, "the request to be queued")
        # No host ever comes.
        wait_until(lambda: "expired" in app.stderr(), 5, "the request to time out")
        check("expired after 2.0 s with no SeoulHost; dropped" in app.stderr(),
              "the request was dropped after its window, with a reason")
        check(app.alive(), "SeoulApp is still running")
    finally:
        app.stop()


def test_9_seoulapp_starts_over_a_stale_socket_file(sock, work):
    """A SeoulApp that was killed leaves a socket FILE behind. bind() fails with
    EADDRINUSE on it forever, so the app must unlink before binding — otherwise
    the host's retry loop spins on a socket nobody can ever own again."""
    try:
        os.unlink(sock)
    except FileNotFoundError:
        pass
    stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    stale.bind(sock)
    stale.close()                       # closed, NOT unlinked
    check(os.path.exists(sock), "a stale socket file is in the way")

    app = RealApp(sock, work, "t9")
    host = FakeHost(sock)
    try:
        wait_until(lambda: "listening on" in app.stderr(), 5,
                   "SeoulApp to bind over the stale file")
        check("FATAL" not in app.stderr(), "SeoulApp did not fail to bind")
        check(host.connect(), "and a host can reach the new listener")
    finally:
        host.close()
        app.stop()


TESTS = [
    ("1. starts and waits when SeoulApp is not running", test_1_starts_without_seoulapp),
    ("2. survives SeoulApp being killed mid-session", test_2_survives_seoulapp_dying_mid_session),
    ("3. stdin EOF exits 0 with a logged reason", test_3_stdin_eof_exits_zero),
    ("4. a message sent while disconnected is delivered", test_4_message_sent_while_disconnected_is_delivered),
    ("5. a full buffer drops the oldest and is not fatal", test_5_buffer_overflow_drops_oldest_and_survives),
    ("6. one host rides out repeated SeoulApp restarts", test_6_survives_repeated_seoulapp_restarts),
    ("7. a hotkey with no host is queued, then sent", test_7_hotkey_with_no_host_is_queued_not_dropped),
    ("8. a queued hotkey expires after its window", test_8_queued_hotkey_expires_after_the_window),
    ("9. SeoulApp starts over a stale socket file", test_9_seoulapp_starts_over_a_stale_socket_file),
]


def main():
    for binary in (HOST_BIN, APP_BIN):
        if not os.path.exists(binary):
            print(f"error: {binary} not found. Run `swift build` first.", file=sys.stderr)
            return 2

    # Short, because sun_path is 104 bytes; and NOT /tmp/seoul.sock, because a
    # SeoulApp the user has running for real must not be part of this.
    sock = f"/tmp/seoul-it-{os.getpid()}.sock"
    work = tempfile.mkdtemp(prefix="seoul-host-tests-")
    print(f"socket: {sock}\nlogs:   {work}\n")

    failures = []
    for name, test in TESTS:
        print(f"==> {name}")
        started = time.monotonic()
        try:
            test(sock, work)
            print(f"    PASS ({time.monotonic() - started:.1f}s)\n")
        except Failure as error:
            failures.append((name, str(error)))
            print(f"    FAIL {error}\n")
        except Exception as error:            # noqa: BLE001 - report, do not abort the run
            failures.append((name, repr(error)))
            print(f"    ERROR {error!r}\n")
        finally:
            try:
                os.unlink(sock)
            except FileNotFoundError:
                pass

    if failures:
        print(f"{len(failures)} of {len(TESTS)} failed:")
        for name, error in failures:
            print(f"  - {name}: {error}")
        print(f"\nhost stderr for each test is in {work}")
        return 1

    print(f"all {len(TESTS)} passed. host stderr in {work}")
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    sys.exit(main())
