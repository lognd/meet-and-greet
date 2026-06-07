from dataclasses import dataclass, field
from typing import Optional


@dataclass
class Student:
    uuid: str
    student_id_enc: str          # XOR-encrypted hex of student ID
    forename: str
    surname: str
    passphrase: str
    registered_at: float         # unix timestamp


@dataclass
class Meeting:
    uuid: str
    finder_uuid: str
    target_uuid: str
    met_at: float
    answers: str                 # JSON array: [{question, answer}, ...]


@dataclass
class Announcement:
    uuid: str
    message: str
    sent_at: float
