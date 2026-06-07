import sys
from pathlib import Path

if sys.version_info >= (3, 11):
    from typing import Self
else:
    from typing_extensions import Self

if sys.version_info >= (3, 11):
    import tomllib
else:
    import tomli as tomllib  # type: ignore[no-redef]

from pydantic import BaseModel, Field


class AppConfig(BaseModel):
    # Network
    port: int = Field(9876, description="HTTP API port")
    udp_port: int = Field(9875, description="UDP discovery port")

    # Security
    encryption_key: str = Field(
        "deadbeefcafebabedeadbeefcafebabe",
        description="32-char hex string (16 bytes). Must match MAG_XORKEY in client build.",
    )
    admin_token: str = Field(
        "change-me-before-use",
        description="Bearer token for admin API endpoints.",
    )

    # Session
    time_limit_minutes: int = Field(
        20,
        description="Session duration in minutes. 0 means no automatic deadline.",
    )
    targets_per_student: int = Field(
        5,
        description="How many targets each student is assigned (k-regular graph).",
    )

    # Storage
    db_file: str = Field("mag.db", description="SQLite database path.")
    targets_file: str = Field(
        "targets.json", description="JSON file persisting target assignments."
    )
    log_file: str = Field("mag.log", description="Rotating log file path.")

    # Behavior
    questions_per_meeting: int = Field(
        3, description="Questions shown per student-student meeting."
    )

    @classmethod
    def from_toml(cls, toml_file: Path) -> Self:
        with open(toml_file, "rb") as f:
            data = tomllib.load(f)
        return cls(**data)
