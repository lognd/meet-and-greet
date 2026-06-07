"""
System-test harness for Meet and Greet.

Provides:
  ServerProcess   - context manager that starts/stops a real server subprocess
                    in an isolated temp directory, cleaning up on exit.
  SimStudent      - simulates a student's full HTTP interaction.
  AdminClient     - thin wrapper for admin-only API calls.
  encrypt_id      - same XOR cipher used by the C++ client (pure Python).
  find_client_bin - locate the compiled mag_client binary cross-platform.
"""

from __future__ import annotations

import json
import os
import platform
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Optional

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).parents[2]
SRC_DIR   = REPO_ROOT / "src"
BUILD_DIR = REPO_ROOT / "build"
PYTHON    = sys.executable

# Platform-aware binary name
_BIN_NAME = "mag_client.exe" if platform.system() == "Windows" else "mag_client"


def find_client_bin() -> Optional[Path]:
    """Return path to the compiled mag_client binary, or None if not built."""
    candidate = BUILD_DIR / _BIN_NAME
    return candidate if candidate.exists() else None


# ---------------------------------------------------------------------------
# Crypto helper (mirrors src/server/crypto/cipher.py)
# ---------------------------------------------------------------------------

def encrypt_id(student_id: int, key_hex: str) -> str:
    key   = bytes.fromhex(key_hex)
    plain = str(student_id).encode()
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(plain)).hex()


# ---------------------------------------------------------------------------
# Low-level HTTP helpers
# ---------------------------------------------------------------------------

def _req(method: str, url: str, body: dict | None = None,
         token: str | None = None) -> dict:
    data    = json.dumps(body).encode() if body is not None else None
    headers: dict[str, str] = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"HTTP {e.code} on {method} {url}: {e.read().decode()}") from e
    except urllib.error.URLError as e:
        raise RuntimeError(f"Connection error on {method} {url}: {e.reason}") from e


def _get(url: str, token: str | None = None)  -> dict:
    return _req("GET",    url, token=token)

def _post(url: str, body: dict, token: str | None = None) -> dict:
    return _req("POST",   url, body, token=token)

def _put(url: str, body: dict, token: str | None = None) -> dict:
    return _req("PUT",    url, body, token=token)

def _delete(url: str, token: str | None = None) -> dict:
    return _req("DELETE", url, token=token)


# ---------------------------------------------------------------------------
# ServerProcess
# ---------------------------------------------------------------------------

class ServerProcess:
    """Start a real MAG server in an isolated temp directory.

    Usage::

        with ServerProcess(port=19880, udp_port=19881) as srv:
            srv.post("/register", {...})
            srv.admin_post("/admin/assign", {})
    """

    DEFAULT_KEY   = "deadbeefcafebabedeadbeefcafebabe"
    DEFAULT_TOKEN = "harness-admin-token"

    def __init__(
        self,
        port: int,
        udp_port: int,
        *,
        enc_key: str = DEFAULT_KEY,
        admin_token: str = DEFAULT_TOKEN,
        targets_per_student: int = 5,
        questions_per_meeting: int = 2,
        time_limit_minutes: int = 0,
    ) -> None:
        self.port                 = port
        self.udp_port             = udp_port
        self.enc_key              = enc_key
        self.admin_token          = admin_token
        self.targets_per_student  = targets_per_student
        self.questions_per_meeting = questions_per_meeting
        self.time_limit_minutes   = time_limit_minutes
        self._tmpdir: tempfile.TemporaryDirectory | None = None
        self._proc:   subprocess.Popen | None = None

    # -- context manager --

    def __enter__(self) -> "ServerProcess":
        self._tmpdir = tempfile.TemporaryDirectory(prefix="mag_srv_")
        tmp = Path(self._tmpdir.name)

        toml = tmp / "app.toml"
        toml.write_text(
            f"""
port                 = {self.port}
udp_port             = {self.udp_port}
encryption_key       = "{self.enc_key}"
admin_token          = "{self.admin_token}"
time_limit_minutes   = {self.time_limit_minutes}
targets_per_student  = {self.targets_per_student}
db_file              = "{tmp / 'mag.db'}"
targets_file         = "{tmp / 'targets.json'}"
log_file             = "{tmp / 'mag.log'}"
questions_per_meeting = {self.questions_per_meeting}
""",
            encoding="utf-8",
        )

        env = {
            **os.environ,
            "PYTHONPATH": str(SRC_DIR),
            "MAG_TOML": str(toml),
        }
        self._proc = subprocess.Popen(
            [PYTHON, "-m", "server"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self._wait_ready()
        return self

    def __exit__(self, *_: object) -> None:
        if self._proc:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait()
        if self._tmpdir:
            self._tmpdir.cleanup()

    def _wait_ready(self, timeout: float = 15.0) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.get("/time")
                return
            except Exception:
                if self._proc.poll() is not None:
                    raise RuntimeError("Server process exited unexpectedly")
                time.sleep(0.15)
        raise RuntimeError(f"Server on port {self.port} did not start within {timeout}s")

    # -- request helpers --

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def get(self, path: str) -> dict:
        return _get(self.base_url + path)

    def post(self, path: str, body: dict) -> dict:
        return _post(self.base_url + path, body)

    def put(self, path: str, body: dict) -> dict:
        return _put(self.base_url + path, body)

    def admin_get(self, path: str) -> dict:
        return _get(self.base_url + path, token=self.admin_token)

    def admin_post(self, path: str, body: dict) -> dict:
        return _post(self.base_url + path, body, token=self.admin_token)

    def admin_delete(self, path: str) -> dict:
        return _delete(self.base_url + path, token=self.admin_token)

    # -- convenience wrappers --

    def assign_targets(self, force: bool = False) -> dict:
        return self.admin_post("/admin/assign", {"force": force})

    def all_students(self) -> list[dict]:
        return self.admin_get("/admin/students")["students"]

    def announce(self, message: str) -> dict:
        return self.admin_post("/admin/announce", {"message": message})


# ---------------------------------------------------------------------------
# SimStudent
# ---------------------------------------------------------------------------

class SimStudent:
    """Simulates a single student's HTTP interaction with the server.

    All network calls are synchronous/blocking.  Designed to be used from
    multiple threads concurrently.
    """

    def __init__(
        self,
        student_id: int,
        forename: str,
        surname: str,
        server: ServerProcess,
    ) -> None:
        self.student_id = student_id
        self.forename   = forename
        self.surname    = surname
        self._srv       = server
        self._enc       = encrypt_id(student_id, server.enc_key)

        # Populated after register()
        self.uuid:       str = ""
        self.passphrase: str = ""
        self.is_new:     bool = True

    # -- registration --

    def register(self) -> "SimStudent":
        r = self._srv.post("/register", {
            "encrypted_id": self._enc,
            "forename": self.forename,
            "surname":  self.surname,
        })
        self.uuid       = r["uuid"]
        self.passphrase = r["passphrase"]
        self.is_new     = r["is_new"]
        return self

    # -- targets --

    def get_targets(self, poll_timeout: float = 30.0) -> list[dict]:
        """Poll until targets are assigned or timeout expires."""
        deadline = time.time() + poll_timeout
        while time.time() < deadline:
            try:
                r = self._srv.get(f"/targets/{self.uuid}")
                if r.get("assigned"):
                    return r["targets"]
            except RuntimeError as e:
                if "404" not in str(e):
                    raise
            time.sleep(0.3)
        raise TimeoutError(f"Targets never assigned for {self.uuid}")

    # -- meeting --

    def meet(self, passphrase: str) -> dict:
        return self._srv.post("/meet", {
            "finder_uuid": self.uuid,
            "passphrase":  passphrase,
        })

    def submit_answers(self, target_uuid: str, questions: list[str]) -> dict:
        return self._srv.post("/answer", {
            "finder_uuid": self.uuid,
            "target_uuid": target_uuid,
            "answers": [{"question": q, "answer": "test answer"} for q in questions],
        })

    # -- stats --

    def get_stats(self) -> dict:
        return self._srv.get(f"/stats/{self.uuid}")

    # -- full flow --

    def run_full_flow(
        self,
        passphrase_map: dict[str, str],
        *,
        poll_timeout: float = 60.0,
    ) -> dict:
        """Register -> wait for targets -> meet all targets -> return stats.

        passphrase_map: {uuid -> passphrase} for ALL students in the session.
        Returns the final stats dict.
        """
        self.register()
        targets = self.get_targets(poll_timeout=poll_timeout)

        for target in targets:
            t_uuid = target["uuid"]
            t_pass = passphrase_map[t_uuid]

            meet_r = self.meet(t_pass)
            if meet_r["ok"]:
                try:
                    questions = meet_r.get("questions", [])
                    self.submit_answers(meet_r["target_uuid"], questions)
                except RuntimeError as exc:
                    if "409" not in str(exc):
                        raise
            elif "already met" in meet_r.get("reason", ""):
                pass  # meeting was recorded from the other side - that's fine
            else:
                raise AssertionError(
                    f"{self.uuid} failed to meet {t_uuid}: {meet_r.get('reason')}"
                )

        return self.get_stats()


# ---------------------------------------------------------------------------
# AdminClient  (thin helper when you don't have a ServerProcess reference)
# ---------------------------------------------------------------------------

class AdminClient:
    def __init__(self, base_url: str, token: str) -> None:
        self._base = base_url
        self._tok  = token

    def get(self, path: str) -> dict:
        return _get(self._base + path, token=self._tok)

    def post(self, path: str, body: dict) -> dict:
        return _post(self._base + path, body, token=self._tok)

    def delete(self, path: str) -> dict:
        return _delete(self._base + path, token=self._tok)
