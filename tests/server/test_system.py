"""System tests: start a real server subprocess, run through the full flow.

These tests exercise the entire stack - server process, HTTP, SQLite, UDP
discovery (skipped in CI where broadcast may be blocked).

Marked slow; run with:   pytest tests/server/test_system.py -v
"""

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import urllib.request
import urllib.error
import pytest


PYTHON = str(pathlib.Path(sys.executable))
SRC    = str(pathlib.Path(__file__).parents[2] / "src")

# Port range chosen to avoid collisions with the dev server
_PORT     = 19870
_UDP_PORT = 19871


def _url(path: str) -> str:
    return f"http://127.0.0.1:{_PORT}{path}"


def _get(path: str) -> dict:
    with urllib.request.urlopen(_url(path), timeout=5) as r:
        return json.loads(r.read())


def _post(path: str, body: dict, token: str | None = None) -> dict:
    data    = json.dumps(body).encode()
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(_url(path), data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read())


def _wait_ready(timeout: float = 10.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            _get("/time")
            return
        except Exception:
            time.sleep(0.2)
    raise RuntimeError("Server did not start in time")


@pytest.fixture(scope="module")
def running_server():
    """Start a real server for the duration of the module's tests."""
    toml_dir  = tempfile.mkdtemp()
    toml_path = pathlib.Path(toml_dir) / "app.toml"
    toml_path.write_text(f"""\
port                = {_PORT}
udp_port            = {_UDP_PORT}
encryption_key      = "deadbeefcafebabedeadbeefcafebabe"
admin_token         = "sys-test-token"
time_limit_minutes  = 0
targets_per_student = 2
db_file             = "{toml_dir}/mag.db"
targets_file        = "{toml_dir}/targets.json"
log_file            = "{toml_dir}/mag.log"
questions_per_meeting = 2
""")

    env = {**os.environ, "PYTHONPATH": SRC, "MAG_TOML": str(toml_path)}
    proc = subprocess.Popen(
        [PYTHON, "-m", "server"],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    try:
        _wait_ready(timeout=15.0)
        yield {"port": _PORT, "token": "sys-test-token"}
    finally:
        proc.terminate()
        proc.wait(timeout=5)


TOKEN = "sys-test-token"


class TestSystemRegistration:
    def test_server_is_reachable(self, running_server):
        data = _get("/time")
        assert "server_time" in data

    def test_register_new_student(self, running_server):
        r = _post("/register", {
            "encrypted_id": "sys-enc-001",
            "forename": "Sys",
            "surname": "Test",
        })
        assert r["is_new"] is True
        assert "uuid" in r
        assert "passphrase" in r

    def test_reconnect_returns_is_new_false(self, running_server):
        _post("/register", {"encrypted_id": "sys-enc-002",
                            "forename": "Dup", "surname": "User"})
        r = _post("/register", {"encrypted_id": "sys-enc-002",
                                "forename": "Dup", "surname": "User"})
        assert r["is_new"] is False

    def test_admin_lists_students(self, running_server):
        with urllib.request.urlopen(
            urllib.request.Request(
                _url("/admin/students"),
                headers={"Authorization": f"Bearer {TOKEN}"},
            ),
            timeout=5,
        ) as resp:
            data = json.loads(resp.read())
        assert data["count"] >= 2   # at least the two registered above


class TestSystemFullFlow:
    """End-to-end: two students register, get targets, meet, check stats."""

    def test_full_two_student_flow(self, running_server):
        # 1. Register Alice
        alice = _post("/register", {
            "encrypted_id": "flow-alice",
            "forename": "Flow",
            "surname": "Alice",
        })
        alice_uuid = alice["uuid"]
        assert alice["is_new"] is True

        # 2. Register Bob
        bob = _post("/register", {
            "encrypted_id": "flow-bob",
            "forename": "Flow",
            "surname": "Bob",
        })
        bob_uuid   = bob["uuid"]
        bob_pass   = bob["passphrase"]

        # 3. Targets not yet assigned
        with pytest.raises(urllib.error.HTTPError) as exc_info:
            _get(f"/targets/{alice_uuid}")
        assert exc_info.value.code == 404

        # 4. Admin assigns targets
        assign_r = _post("/admin/assign", {"force": True}, token=TOKEN)
        assert assign_r["ok"] is True

        # 5. Alice fetches her targets
        targets_r = _get(f"/targets/{alice_uuid}")
        assert targets_r["assigned"] is True
        targets_list = targets_r["targets"]
        assert len(targets_list) > 0

        # Pick Alice's first target (whoever it is) and meet them
        first_target = targets_list[0]
        first_uuid = first_target["uuid"]

        # Fetch that target's passphrase from admin list
        with urllib.request.urlopen(
            urllib.request.Request(
                _url("/admin/students"),
                headers={"Authorization": f"Bearer {TOKEN}"},
            ),
            timeout=5,
        ) as resp:
            all_students = json.loads(resp.read())["students"]
        target_pass = next(
            s["passphrase"] for s in all_students if s["uuid"] == first_uuid
        )

        # 6. Alice meets the target
        meet_r = _post("/meet", {
            "finder_uuid": alice_uuid,
            "passphrase": target_pass,
        })
        assert meet_r["ok"] is True
        questions = meet_r["questions"]
        assert len(questions) == 2

        # 7. Alice submits answers
        answer_r = _post("/answer", {
            "finder_uuid": alice_uuid,
            "target_uuid": first_uuid,
            "answers": [{"question": q, "answer": "test"} for q in questions],
        })
        assert answer_r["ok"] is True
        assert answer_r["meetings_completed"] == 1

        # 8. Alice's stats show completion
        stats = _get(f"/stats/{alice_uuid}")
        assert stats["meetings_completed"] >= 1

    def test_wrong_passphrase_fails(self, running_server):
        a = _post("/register", {"encrypted_id": "wp-alice",
                                "forename": "WP", "surname": "Alice"})
        meet_r = _post("/meet", {
            "finder_uuid": a["uuid"],
            "passphrase": "completely-made-up-passphrase",
        })
        assert meet_r["ok"] is False

    def test_announcement_flow(self, running_server):
        # Post announcement, then retrieve it
        _post("/admin/announce", {"message": "System test announce"}, token=TOKEN)
        anns = _get("/announcements")["announcements"]
        messages = [a["message"] for a in anns]
        assert "System test announce" in messages

    def test_time_deadline_set_by_admin(self, running_server):
        import time as _time
        future = _time.time() + 3600
        _post("/admin/time", {"deadline": future}, token=TOKEN)
        t = _get("/time")
        assert t["remaining_seconds"] is not None
        assert t["remaining_seconds"] > 0
