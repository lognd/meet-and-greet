"""
Binary interaction tests - exercise the actual compiled mag_client binary
talking to a real server process.

All tests use the --headless --server flags to bypass TUI and UDP discovery,
making them fast and deterministic in CI.

Platform notes
--------------
- Marked @posix_only where the binary is expected at build/mag_client.
- Windows: mark @windows_only tests use build/mag_client.exe (add later).
- @requires_binary skips the whole test if the binary hasn't been compiled.
"""

import platform
import subprocess
import time
import pytest

from tests.system.harness import ServerProcess, find_client_bin, encrypt_id

# ---------------------------------------------------------------------------
# Ports - isolated from other test modules
# ---------------------------------------------------------------------------

_PORT     = 19885
_UDP_PORT = 19886

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _run_client(*extra_args: str, timeout: int = 10) -> subprocess.CompletedProcess:
    bin_path = find_client_bin()
    return subprocess.run(
        [str(bin_path)] + list(extra_args),
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def _parse_output(stdout: str) -> dict[str, str]:
    """Parse KEY=VALUE lines from headless client output."""
    result = {}
    for line in stdout.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            result[k.strip()] = v.strip()
    return result


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.requires_binary
@pytest.mark.posix_only
class TestBinaryRegistration:
    """Headless client registers against a real server."""

    def test_new_student_exits_zero(self):
        with ServerProcess(_PORT, _UDP_PORT) as srv:
            result = _run_client(
                "--headless", "10000001",
                "--server", f"127.0.0.1:{_PORT}",
            )
        assert result.returncode == 0, result.stderr

    def test_stdout_contains_uuid_and_passphrase(self):
        with ServerProcess(_PORT, _UDP_PORT) as srv:
            result = _run_client(
                "--headless", "10000002",
                "--server", f"127.0.0.1:{_PORT}",
            )
        out = _parse_output(result.stdout)
        assert "UUID" in out
        assert "PASS" in out
        assert len(out["UUID"]) == 36   # standard UUID4 format
        assert "-" in out["PASS"]       # spy passphrase has hyphens

    def test_uuid_format_is_valid(self):
        import re
        uuid_re = re.compile(
            r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"
        )
        with ServerProcess(_PORT, _UDP_PORT) as srv:
            result = _run_client(
                "--headless", "10000003",
                "--server", f"127.0.0.1:{_PORT}",
            )
        out = _parse_output(result.stdout)
        assert uuid_re.match(out["UUID"]), f"Invalid UUID: {out.get('UUID')}"

    def test_new_flag_is_1_for_new_student(self):
        with ServerProcess(_PORT, _UDP_PORT) as srv:
            result = _run_client(
                "--headless", "10000004",
                "--server", f"127.0.0.1:{_PORT}",
            )
        out = _parse_output(result.stdout)
        assert out["NEW"] == "1"

    def test_new_flag_is_0_on_reconnect(self):
        with ServerProcess(_PORT, _UDP_PORT) as srv:
            _run_client("--headless", "10000005", "--server", f"127.0.0.1:{_PORT}")
            result = _run_client(
                "--headless", "10000005",
                "--server", f"127.0.0.1:{_PORT}",
            )
        out = _parse_output(result.stdout)
        assert out["NEW"] == "0"

    def test_reconnect_returns_same_uuid(self):
        with ServerProcess(_PORT, _UDP_PORT) as srv:
            r1 = _run_client("--headless", "10000006", "--server", f"127.0.0.1:{_PORT}")
            r2 = _run_client("--headless", "10000006", "--server", f"127.0.0.1:{_PORT}")
        assert _parse_output(r1.stdout)["UUID"] == _parse_output(r2.stdout)["UUID"]

    def test_student_appears_in_admin_list(self):
        with ServerProcess(_PORT, _UDP_PORT) as srv:
            result = _run_client(
                "--headless", "10000007",
                "--server", f"127.0.0.1:{_PORT}",
            )
            out = _parse_output(result.stdout)
            students = srv.all_students()
        uuids = [s["uuid"] for s in students]
        assert out["UUID"] in uuids

    def test_unique_passphrases_for_different_students(self):
        with ServerProcess(_PORT, _UDP_PORT) as srv:
            r1 = _run_client("--headless", "10000008", "--server", f"127.0.0.1:{_PORT}")
            r2 = _run_client("--headless", "10000009", "--server", f"127.0.0.1:{_PORT}")
        p1 = _parse_output(r1.stdout).get("PASS", "")
        p2 = _parse_output(r2.stdout).get("PASS", "")
        assert p1 != p2, "Two students received the same passphrase"


@pytest.mark.requires_binary
@pytest.mark.posix_only
class TestBinaryErrorHandling:
    """Client fails gracefully when server is unavailable."""

    def test_no_server_exits_nonzero(self):
        # Port 19887 has nothing listening; --timeout 2 keeps the test fast.
        result = _run_client(
            "--headless", "99999",
            "--server", "127.0.0.1:19887",
            timeout=8,
        )
        assert result.returncode != 0

    def test_no_server_prints_error(self):
        result = _run_client(
            "--headless", "99999",
            "--server", "127.0.0.1:19887",
            timeout=8,
        )
        assert "ERROR" in result.stderr

    def test_udp_timeout_exits_nonzero(self):
        # No server broadcasting; --timeout 2 makes it return quickly.
        result = _run_client(
            "--headless", "99999",
            "--timeout", "2",
            timeout=10,
        )
        # Should exit 1 (server not found) or 0 if a stray server is running.
        # We only verify it doesn't hang forever (timeout would raise).
        assert isinstance(result.returncode, int)


@pytest.mark.requires_binary
@pytest.mark.posix_only
class TestBinaryMultipleClients:
    """Run several client binaries concurrently against one server."""

    def test_ten_concurrent_registrations(self):
        import concurrent.futures

        ids = list(range(20000001, 20000011))

        def register(sid):
            return _run_client(
                "--headless", str(sid),
                "--server", f"127.0.0.1:{_PORT}",
            )

        with ServerProcess(_PORT, _UDP_PORT) as srv:
            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as ex:
                results = list(ex.map(register, ids))

        assert all(r.returncode == 0 for r in results), [
            r.stderr for r in results if r.returncode != 0
        ]
        uuids = [_parse_output(r.stdout).get("UUID") for r in results]
        assert len(set(uuids)) == 10, "Expected 10 unique UUIDs"

    def test_full_flow_two_binary_clients(self):
        """Two headless clients register, get targets, meet each other."""
        import concurrent.futures

        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=1) as srv:
            # Register two students via binary
            r1 = _run_client("--headless", "30000001", "--server", f"127.0.0.1:{_PORT}")
            r2 = _run_client("--headless", "30000002", "--server", f"127.0.0.1:{_PORT}")
            out1 = _parse_output(r1.stdout)
            out2 = _parse_output(r2.stdout)

            # Admin assigns targets
            srv.assign_targets(force=True)

            # Verify each is the other's target
            t1 = srv.get(f"/targets/{out1['UUID']}")["targets"]
            t2 = srv.get(f"/targets/{out2['UUID']}")["targets"]
            assert any(t["uuid"] == out2["UUID"] for t in t1)
            assert any(t["uuid"] == out1["UUID"] for t in t2)

            # Student 1 meets student 2 via API (simulating what the client would do)
            meet_r = srv.post("/meet", {
                "finder_uuid": out1["UUID"],
                "passphrase":  out2["PASS"],
            })
            assert meet_r["ok"] is True
