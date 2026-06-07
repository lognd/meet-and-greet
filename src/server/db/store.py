"""SQLite data store. All operations are synchronous; FastAPI runs them in a
thread pool automatically when route handlers are plain (non-async) functions."""

import sqlite3
import json
import time
from pathlib import Path
from typing import Optional

from server.db.models import Student, Meeting, Announcement


_SCHEMA = """
PRAGMA journal_mode=WAL;

CREATE TABLE IF NOT EXISTS students (
    uuid            TEXT PRIMARY KEY,
    student_id_enc  TEXT UNIQUE NOT NULL,
    forename        TEXT NOT NULL,
    surname         TEXT NOT NULL,
    passphrase      TEXT NOT NULL,
    registered_at   REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS meetings (
    uuid        TEXT PRIMARY KEY,
    finder_uuid TEXT NOT NULL REFERENCES students(uuid) ON DELETE CASCADE,
    target_uuid TEXT NOT NULL REFERENCES students(uuid) ON DELETE CASCADE,
    met_at      REAL NOT NULL,
    answers     TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE IF NOT EXISTS announcements (
    uuid    TEXT PRIMARY KEY,
    message TEXT NOT NULL,
    sent_at REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS server_state (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
"""


class DataStore:
    def __init__(self, db_path: str) -> None:
        self._path = db_path
        self._conn = sqlite3.connect(db_path, check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._conn.executescript(_SCHEMA)
        self._conn.commit()

    def close(self) -> None:
        self._conn.close()

    # --- students ---

    def add_student(self, student: Student) -> None:
        self._conn.execute(
            "INSERT INTO students VALUES (?,?,?,?,?,?)",
            (
                student.uuid,
                student.student_id_enc,
                student.forename,
                student.surname,
                student.passphrase,
                student.registered_at,
            ),
        )
        self._conn.commit()

    def get_student_by_uuid(self, uuid: str) -> Optional[Student]:
        row = self._conn.execute(
            "SELECT * FROM students WHERE uuid=?", (uuid,)
        ).fetchone()
        return _row_to_student(row) if row else None

    def get_student_by_enc_id(self, enc_id: str) -> Optional[Student]:
        row = self._conn.execute(
            "SELECT * FROM students WHERE student_id_enc=?", (enc_id,)
        ).fetchone()
        return _row_to_student(row) if row else None

    def get_student_by_passphrase(self, passphrase: str) -> Optional[Student]:
        row = self._conn.execute(
            "SELECT * FROM students WHERE passphrase=?", (passphrase,)
        ).fetchone()
        return _row_to_student(row) if row else None

    def update_student_name(self, uuid: str, forename: str, surname: str) -> None:
        self._conn.execute(
            "UPDATE students SET forename=?, surname=? WHERE uuid=?",
            (forename, surname, uuid),
        )
        self._conn.commit()

    def delete_student(self, uuid: str) -> bool:
        cur = self._conn.execute("DELETE FROM students WHERE uuid=?", (uuid,))
        self._conn.commit()
        return cur.rowcount > 0

    def all_students(self) -> list[Student]:
        rows = self._conn.execute("SELECT * FROM students ORDER BY registered_at").fetchall()
        return [_row_to_student(r) for r in rows]

    def used_passphrases(self) -> set[str]:
        rows = self._conn.execute("SELECT passphrase FROM students").fetchall()
        return {r["passphrase"] for r in rows}

    # --- meetings ---

    def add_meeting(self, meeting: Meeting) -> None:
        self._conn.execute(
            "INSERT INTO meetings VALUES (?,?,?,?,?)",
            (
                meeting.uuid,
                meeting.finder_uuid,
                meeting.target_uuid,
                meeting.met_at,
                meeting.answers,
            ),
        )
        self._conn.commit()

    def meeting_exists(self, finder_uuid: str, target_uuid: str) -> bool:
        row = self._conn.execute(
            """SELECT 1 FROM meetings
               WHERE (finder_uuid=? AND target_uuid=?)
                  OR (finder_uuid=? AND target_uuid=?)""",
            (finder_uuid, target_uuid, target_uuid, finder_uuid),
        ).fetchone()
        return row is not None

    def meetings_for_student(self, uuid: str) -> list[Meeting]:
        rows = self._conn.execute(
            """SELECT * FROM meetings
               WHERE finder_uuid=? OR target_uuid=?
               ORDER BY met_at""",
            (uuid, uuid),
        ).fetchall()
        return [_row_to_meeting(r) for r in rows]

    def all_finished_students(self) -> list[tuple[str, float]]:
        """Return (uuid, finished_at) for students who completed all targets,
        ordered by finish time. The caller supplies total_targets for filtering."""
        rows = self._conn.execute(
            """SELECT finder_uuid AS uuid, MAX(met_at) AS finished_at,
                      COUNT(*) AS cnt
               FROM meetings
               GROUP BY finder_uuid
               ORDER BY finished_at""",
        ).fetchall()
        return [(r["uuid"], r["finished_at"]) for r in rows]

    # --- announcements ---

    def add_announcement(self, ann: Announcement) -> None:
        self._conn.execute(
            "INSERT INTO announcements VALUES (?,?,?)",
            (ann.uuid, ann.message, ann.sent_at),
        )
        self._conn.commit()

    def announcements_since(self, since: float) -> list[Announcement]:
        rows = self._conn.execute(
            "SELECT * FROM announcements WHERE sent_at > ? ORDER BY sent_at",
            (since,),
        ).fetchall()
        return [_row_to_ann(r) for r in rows]

    # --- server state (deadline, etc.) ---

    def set_state(self, key: str, value: str) -> None:
        self._conn.execute(
            "INSERT OR REPLACE INTO server_state VALUES (?,?)", (key, value)
        )
        self._conn.commit()

    def get_state(self, key: str) -> Optional[str]:
        row = self._conn.execute(
            "SELECT value FROM server_state WHERE key=?", (key,)
        ).fetchone()
        return row["value"] if row else None


def _row_to_student(row: sqlite3.Row) -> Student:
    return Student.model_validate(dict(row))


def _row_to_meeting(row: sqlite3.Row) -> Meeting:
    return Meeting.model_validate(dict(row))


def _row_to_ann(row: sqlite3.Row) -> Announcement:
    return Announcement.model_validate(dict(row))
