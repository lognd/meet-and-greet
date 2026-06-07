import tempfile
import pathlib
import pytest
from pydantic import ValidationError
from server.app.config import AppConfig


MINIMAL_TOML = """\
encryption_key = "deadbeefcafebabedeadbeefcafebabe"
admin_token    = "secret"
"""

FULL_TOML = """\
port                = 8888
udp_port            = 8887
encryption_key      = "aabbccddaabbccddaabbccddaabbccdd"
admin_token         = "mytoken"
time_limit_minutes  = 30
targets_per_student = 3
db_file             = "test.db"
targets_file        = "test_targets.json"
log_file            = "test.log"
questions_per_meeting = 4
"""


def _write_toml(content: str) -> pathlib.Path:
    f = tempfile.NamedTemporaryFile(suffix=".toml", delete=False, mode="w")
    f.write(content)
    f.close()
    return pathlib.Path(f.name)


class TestAppConfigDefaults:
    def test_defaults_applied(self):
        cfg = AppConfig(encryption_key="deadbeefcafebabedeadbeefcafebabe",
                        admin_token="x")
        assert cfg.port == 9876
        assert cfg.udp_port == 9875
        assert cfg.time_limit_minutes == 20
        assert cfg.targets_per_student == 5
        assert cfg.questions_per_meeting == 3

    def test_from_toml_minimal(self):
        cfg = AppConfig.from_toml(_write_toml(MINIMAL_TOML))
        assert cfg.port == 9876            # default
        assert cfg.admin_token == "secret"

    def test_from_toml_full(self):
        cfg = AppConfig.from_toml(_write_toml(FULL_TOML))
        assert cfg.port == 8888
        assert cfg.targets_per_student == 3
        assert cfg.questions_per_meeting == 4
        assert cfg.db_file == "test.db"

    def test_missing_toml_raises(self):
        with pytest.raises(FileNotFoundError):
            AppConfig.from_toml(pathlib.Path("/nonexistent/path/app.toml"))

    def test_invalid_field_type_raises(self):
        with pytest.raises((ValidationError, Exception)):
            AppConfig(port="not-a-number",
                      encryption_key="deadbeefcafebabedeadbeefcafebabe",
                      admin_token="x")
