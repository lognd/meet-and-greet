import time
import sqlite3
import pytest
from server.db.store import DataStore
from server.db.models import Student, Meeting, Announcement


@pytest.fixture()
def store():
    return DataStore(":memory:")


def _student(suffix="a", **kw) -> Student:
    base = dict(
        uuid=f"uuid-{suffix}",
        student_id_enc=f"enc-{suffix}",
        forename="Alice",
        surname="Alpha",
        passphrase=f"pass-{suffix}",
        registered_at=1000.0 + ord(suffix[0]),
    )
    return Student(**{**base, **kw})


class TestStudentCRUD:
    def test_add_retrieve_by_uuid(self, store):
        store.add_student(_student("a"))
        s = store.get_student_by_uuid("uuid-a")
        assert s is not None and s.forename == "Alice"

    def test_retrieve_by_enc_id(self, store):
        store.add_student(_student("b"))
        s = store.get_student_by_enc_id("enc-b")
        assert s is not None and s.uuid == "uuid-b"

    def test_retrieve_by_passphrase(self, store):
        store.add_student(_student("c"))
        s = store.get_student_by_passphrase("pass-c")
        assert s is not None and s.uuid == "uuid-c"

    def test_missing_returns_none(self, store):
        assert store.get_student_by_uuid("nope") is None
        assert store.get_student_by_enc_id("nope") is None
        assert store.get_student_by_passphrase("nope") is None

    def test_update_name(self, store):
        store.add_student(_student("d"))
        store.update_student_name("uuid-d", "Diane", "Delta")
        s = store.get_student_by_uuid("uuid-d")
        assert s.forename == "Diane" and s.surname == "Delta"

    def test_delete_returns_true(self, store):
        store.add_student(_student("e"))
        assert store.delete_student("uuid-e") is True
        assert store.get_student_by_uuid("uuid-e") is None

    def test_delete_missing_returns_false(self, store):
        assert store.delete_student("ghost") is False

    def test_all_students_ordered(self, store):
        store.add_student(_student("y", registered_at=2000.0))
        store.add_student(_student("x", registered_at=1000.0))
        uuids = [s.uuid for s in store.all_students()]
        assert uuids == ["uuid-x", "uuid-y"]

    def test_used_passphrases(self, store):
        store.add_student(_student("p1", passphrase="alpha"))
        store.add_student(_student("p2", passphrase="beta"))
        assert store.used_passphrases() == {"alpha", "beta"}

    def test_duplicate_enc_id_raises(self, store):
        store.add_student(_student("dup"))
        with pytest.raises(sqlite3.IntegrityError):
            store.add_student(_student("dup2", student_id_enc="enc-dup"))


class TestMeetings:
    def _setup(self, store):
        store.add_student(_student("f"))
        store.add_student(_student("g", forename="Bob"))

    def test_meeting_exists_after_add(self, store):
        self._setup(store)
        store.add_meeting(Meeting(uuid="m1", finder_uuid="uuid-f",
                                  target_uuid="uuid-g", met_at=1.0))
        assert store.meeting_exists("uuid-f", "uuid-g") is True

    def test_meeting_bidirectional(self, store):
        self._setup(store)
        store.add_meeting(Meeting(uuid="m2", finder_uuid="uuid-f",
                                  target_uuid="uuid-g", met_at=1.0))
        assert store.meeting_exists("uuid-g", "uuid-f") is True

    def test_no_meeting(self, store):
        self._setup(store)
        assert store.meeting_exists("uuid-f", "uuid-g") is False

    def test_meetings_for_student_counts_both_sides(self, store):
        self._setup(store)
        store.add_meeting(Meeting(uuid="m3", finder_uuid="uuid-f",
                                  target_uuid="uuid-g", met_at=1.0))
        assert len(store.meetings_for_student("uuid-f")) == 1
        assert len(store.meetings_for_student("uuid-g")) == 1


class TestAnnouncements:
    def test_add_and_retrieve(self, store):
        store.add_announcement(Announcement(uuid="a1", message="Hi!", sent_at=100.0))
        results = store.announcements_since(0.0)
        assert len(results) == 1 and results[0].message == "Hi!"

    def test_since_filter(self, store):
        store.add_announcement(Announcement(uuid="a1", message="old", sent_at=100.0))
        store.add_announcement(Announcement(uuid="a2", message="new", sent_at=200.0))
        results = store.announcements_since(150.0)
        assert len(results) == 1 and results[0].message == "new"

    def test_empty(self, store):
        assert store.announcements_since(0.0) == []


class TestServerState:
    def test_set_and_get(self, store):
        store.set_state("deadline", "99.5")
        assert store.get_state("deadline") == "99.5"

    def test_overwrite(self, store):
        store.set_state("k", "v1")
        store.set_state("k", "v2")
        assert store.get_state("k") == "v2"

    def test_missing_returns_none(self, store):
        assert store.get_state("nope") is None
