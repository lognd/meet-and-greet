import pytest
from pydantic import ValidationError
from server.db.models import Student, Meeting, Announcement


class TestStudent:
    BASE = dict(uuid="u1", student_id_enc="enc", forename="Jane",
                surname="Smith", passphrase="eagle-has-landed", registered_at=1e6)

    def test_valid(self):
        s = Student(**self.BASE)
        assert s.forename == "Jane"

    def test_missing_uuid_raises(self):
        d = {k: v for k, v in self.BASE.items() if k != "uuid"}
        with pytest.raises(ValidationError):
            Student(**d)

    def test_missing_forename_raises(self):
        d = {k: v for k, v in self.BASE.items() if k != "forename"}
        with pytest.raises(ValidationError):
            Student(**d)

    def test_non_numeric_registered_at_raises(self):
        with pytest.raises(ValidationError):
            Student(**{**self.BASE, "registered_at": "not-a-number"})

    def test_model_dump_keys(self):
        s = Student(**self.BASE)
        assert set(s.model_dump()) == {
            "uuid", "student_id_enc", "forename", "surname",
            "passphrase", "registered_at",
        }

    def test_model_validate_from_dict(self):
        s = Student.model_validate(self.BASE)
        assert s.uuid == "u1"


class TestMeeting:
    def test_valid(self):
        m = Meeting(uuid="m1", finder_uuid="f", target_uuid="t", met_at=1.0)
        assert m.answers == "[]"

    def test_answers_default(self):
        m = Meeting(uuid="m", finder_uuid="f", target_uuid="t", met_at=1.0)
        assert m.answers == "[]"

    def test_missing_finder_raises(self):
        with pytest.raises(ValidationError):
            Meeting(uuid="m", target_uuid="t", met_at=1.0)


class TestAnnouncement:
    def test_valid(self):
        a = Announcement(uuid="a1", message="Hi!", sent_at=1.0)
        assert a.message == "Hi!"

    def test_missing_message_raises(self):
        with pytest.raises(ValidationError):
            Announcement(uuid="a1", sent_at=1.0)

    def test_model_validate(self):
        a = Announcement.model_validate({"uuid": "x", "message": "m", "sent_at": 5.0})
        assert a.sent_at == 5.0
