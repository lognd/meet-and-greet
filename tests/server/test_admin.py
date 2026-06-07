"""Integration tests for admin-only routes."""

import pytest
from tests.server.conftest import TEST_TOKEN


AUTH = {"Authorization": f"Bearer {TEST_TOKEN}"}
BAD  = {"Authorization": "Bearer wrong-token"}


class TestAuth:
    def test_no_token_rejected(self, client):
        r = client.get("/admin/students")
        assert r.status_code in (401, 403)

    def test_wrong_token_rejected(self, client):
        r = client.get("/admin/students", headers=BAD)
        assert r.status_code == 401

    def test_correct_token_accepted(self, client):
        r = client.get("/admin/students", headers=AUTH)
        assert r.status_code == 200


class TestListStudents:
    def test_empty(self, client):
        r = client.get("/admin/students", headers=AUTH)
        assert r.json()["count"] == 0

    def test_lists_registered_students(self, client_with_targets):
        r = client_with_targets.get("/admin/students", headers=AUTH)
        body = r.json()
        assert body["count"] == 2
        names = {s["forename"] for s in body["students"]}
        assert names == {"Alice", "Bob"}

    def test_response_includes_passphrase(self, client_with_targets):
        r = client_with_targets.get("/admin/students", headers=AUTH)
        student = r.json()["students"][0]
        assert "passphrase" in student


class TestDeleteStudent:
    def test_delete_removes_student(self, client_with_targets):
        client_with_targets.delete("/admin/student/uuid-alice", headers=AUTH)
        r = client_with_targets.get("/admin/students", headers=AUTH)
        assert r.json()["count"] == 1

    def test_delete_nonexistent_returns_404(self, client):
        r = client.delete("/admin/student/ghost", headers=AUTH)
        assert r.status_code == 404

    def test_delete_removes_from_targets(self, client_with_targets):
        client_with_targets.delete("/admin/student/uuid-alice", headers=AUTH)
        r = client_with_targets.get("/targets/uuid-bob", headers=AUTH)
        # Bob's target list should no longer include alice
        if r.status_code == 200:
            uuids = [t["uuid"] for t in r.json()["targets"]]
            assert "uuid-alice" not in uuids


class TestAssign:
    def test_assign_with_no_students(self, client):
        r = client.post("/admin/assign", json={}, headers=AUTH)
        assert r.status_code == 200
        assert r.json()["students"] == 0

    def test_assign_sets_targets(self, client):
        # Register two students
        client.post("/register", json={"encrypted_id": "e1", "forename": "A", "surname": "B"})
        client.post("/register", json={"encrypted_id": "e2", "forename": "C", "surname": "D"})
        r = client.post("/admin/assign", json={}, headers=AUTH)
        assert r.json()["ok"] is True
        assert r.json()["students"] == 2

    def test_reassign_requires_force(self, client):
        client.post("/register", json={"encrypted_id": "e1", "forename": "A", "surname": "B"})
        client.post("/admin/assign", json={}, headers=AUTH)
        r = client.post("/admin/assign", json={}, headers=AUTH)
        assert r.json()["ok"] is False

    def test_force_reassign(self, client):
        client.post("/register", json={"encrypted_id": "e1", "forename": "A", "surname": "B"})
        client.post("/admin/assign", json={}, headers=AUTH)
        r = client.post("/admin/assign", json={"force": True}, headers=AUTH)
        assert r.json()["ok"] is True


class TestAnnounce:
    def test_announce_stores_message(self, client):
        r = client.post("/admin/announce", json={"message": "Hello class!"}, headers=AUTH)
        assert r.status_code == 200
        assert r.json()["ok"] is True
        anns = client.get("/announcements").json()["announcements"]
        assert len(anns) == 1
        assert anns[0]["message"] == "Hello class!"

    def test_multiple_announcements_accumulate(self, client):
        client.post("/admin/announce", json={"message": "M1"}, headers=AUTH)
        client.post("/admin/announce", json={"message": "M2"}, headers=AUTH)
        anns = client.get("/announcements").json()["announcements"]
        assert len(anns) == 2


class TestSetTime:
    def test_set_absolute_deadline(self, client):
        r = client.post("/admin/time", json={"deadline": 9999.0}, headers=AUTH)
        assert r.status_code == 200
        assert r.json()["deadline"] == pytest.approx(9999.0)

    def test_add_minutes(self, client):
        # Set base deadline first
        client.post("/admin/time", json={"deadline": 1000.0}, headers=AUTH)
        r = client.post("/admin/time", json={"add_minutes": 5.0}, headers=AUTH)
        # 1000 + 5*60 = 1300
        assert r.json()["deadline"] == pytest.approx(1300.0)

    def test_neither_field_is_400(self, client):
        r = client.post("/admin/time", json={}, headers=AUTH)
        assert r.status_code == 400
