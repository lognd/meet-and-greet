"""Integration tests for student-facing routes via FastAPI TestClient."""

import pytest
from tests.server.conftest import make_config


class TestRegister:
    def test_new_student(self, client):
        r = client.post("/register", json={
            "encrypted_id": "abcdef01", "forename": "Jane", "surname": "Smith"
        })
        assert r.status_code == 200
        body = r.json()
        assert body["is_new"] is True
        assert "uuid" in body
        assert "passphrase" in body

    def test_reconnect_same_id(self, client):
        payload = {"encrypted_id": "deadbeef", "forename": "Jane", "surname": "Smith"}
        r1 = client.post("/register", json=payload)
        r2 = client.post("/register", json=payload)
        assert r2.status_code == 200
        assert r2.json()["is_new"] is False
        assert r2.json()["uuid"] == r1.json()["uuid"]

    def test_reconnect_carries_stored_name(self, client):
        client.post("/register", json={
            "encrypted_id": "aabbccdd", "forename": "Alice", "surname": "Alpha"
        })
        r = client.post("/register", json={
            "encrypted_id": "aabbccdd", "forename": "WRONG", "surname": "NAME"
        })
        body = r.json()
        assert body["forename"] == "Alice"
        assert body["name_differs"] is True

    def test_passphrase_is_unique_per_student(self, client):
        r1 = client.post("/register", json={
            "encrypted_id": "enc1", "forename": "A", "surname": "B"
        })
        r2 = client.post("/register", json={
            "encrypted_id": "enc2", "forename": "C", "surname": "D"
        })
        assert r1.json()["passphrase"] != r2.json()["passphrase"]


class TestUpdateName:
    def test_update_succeeds(self, client):
        reg = client.post("/register", json={
            "encrypted_id": "up1", "forename": "Old", "surname": "Name"
        }).json()
        r = client.put(f"/student/{reg['uuid']}", json={
            "forename": "New", "surname": "Name"
        })
        assert r.status_code == 200
        assert r.json()["ok"] is True

    def test_update_nonexistent_404(self, client):
        r = client.put("/student/ghost", json={"forename": "A", "surname": "B"})
        assert r.status_code == 404


class TestTargets:
    def test_targets_not_assigned_returns_404(self, client):
        reg = client.post("/register", json={
            "encrypted_id": "t1", "forename": "A", "surname": "B"
        }).json()
        r = client.get(f"/targets/{reg['uuid']}")
        assert r.status_code == 404

    def test_targets_returned_when_assigned(self, client_with_targets):
        r = client_with_targets.get("/targets/uuid-alice")
        assert r.status_code == 200
        body = r.json()
        assert body["assigned"] is True
        assert len(body["targets"]) == 1
        assert body["targets"][0]["forename"] == "Bob"

    def test_target_includes_passphrase_hint(self, client_with_targets):
        r = client_with_targets.get("/targets/uuid-alice")
        hint = r.json()["targets"][0]["passphrase_hint"]
        # hint is first word of "fox-in-the-henhouse"
        assert hint == "fox"


class TestMeet:
    def test_correct_passphrase_returns_questions(self, client_with_targets):
        r = client_with_targets.post("/meet", json={
            "finder_uuid": "uuid-alice",
            "passphrase": "fox-in-the-henhouse",
        })
        assert r.status_code == 200
        body = r.json()
        assert body["ok"] is True
        assert body["target_uuid"] == "uuid-bob"
        assert len(body["questions"]) == 2   # questions_per_meeting=2 in test cfg

    def test_wrong_passphrase_returns_not_ok(self, client_with_targets):
        r = client_with_targets.post("/meet", json={
            "finder_uuid": "uuid-alice",
            "passphrase": "completely-wrong",
        })
        assert r.status_code == 200
        assert r.json()["ok"] is False

    def test_own_passphrase_rejected(self, client_with_targets):
        r = client_with_targets.post("/meet", json={
            "finder_uuid": "uuid-alice",
            "passphrase": "eagle-has-landed",   # Alice's own passphrase
        })
        assert r.json()["ok"] is False

    def test_non_target_passphrase_rejected(self, client_with_targets):
        # Register a third student whose passphrase Alice didn't get as a target
        charlie = client_with_targets.post("/register", json={
            "encrypted_id": "enc-charlie",
            "forename": "Charlie", "surname": "C",
        }).json()
        # Charlie is not in Alice's target list, so his passphrase should fail
        r = client_with_targets.post("/meet", json={
            "finder_uuid": "uuid-alice",
            "passphrase": charlie["passphrase"],
        })
        assert r.json()["ok"] is False


class TestAnswer:
    def test_submit_records_meeting(self, client_with_targets):
        # First authenticate
        meet_r = client_with_targets.post("/meet", json={
            "finder_uuid": "uuid-alice",
            "passphrase": "fox-in-the-henhouse",
        }).json()
        questions = meet_r["questions"]

        r = client_with_targets.post("/answer", json={
            "finder_uuid": "uuid-alice",
            "target_uuid": "uuid-bob",
            "answers": [{"question": q, "answer": "ans"} for q in questions],
        })
        assert r.status_code == 200
        assert r.json()["ok"] is True
        assert r.json()["meetings_completed"] == 1

    def test_duplicate_answer_rejected(self, client_with_targets):
        payload = {"finder_uuid": "uuid-alice", "target_uuid": "uuid-bob", "answers": []}
        client_with_targets.post("/answer", json=payload)
        r = client_with_targets.post("/answer", json=payload)
        assert r.status_code == 409


class TestStats:
    def test_stats_before_meetings(self, client_with_targets):
        r = client_with_targets.get("/stats/uuid-alice")
        assert r.status_code == 200
        assert r.json()["meetings_completed"] == 0
        assert r.json()["total_targets"] == 1

    def test_stats_after_meeting(self, client_with_targets):
        client_with_targets.post("/answer", json={
            "finder_uuid": "uuid-alice",
            "target_uuid": "uuid-bob",
            "answers": [],
        })
        r = client_with_targets.get("/stats/uuid-alice")
        body = r.json()
        assert body["meetings_completed"] == 1
        assert body["finish_place"] == 1
        assert body["finish_ordinal"] == "1st"


class TestTime:
    def test_time_returns_server_time(self, client):
        r = client.get("/time")
        assert r.status_code == 200
        assert "server_time" in r.json()

    def test_no_deadline_when_time_limit_zero(self, client):
        r = client.get("/time")
        assert r.json()["deadline"] is None


class TestAnnouncements:
    def test_empty_initially(self, client):
        r = client.get("/announcements")
        assert r.status_code == 200
        assert r.json()["announcements"] == []

    def test_since_filter(self, client, store, cfg):
        from server.db.models import Announcement
        store.add_announcement(Announcement(uuid="a1", message="old", sent_at=10.0))
        store.add_announcement(Announcement(uuid="a2", message="new", sent_at=200.0))
        r = client.get("/announcements?since=100.0")
        anns = r.json()["announcements"]
        assert len(anns) == 1
        assert anns[0]["message"] == "new"
