"""Student-facing FastAPI routes."""

import json
import random
import time
import uuid as _uuid
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, Request
from pydantic import BaseModel

from server.db import DataStore, Student, Meeting, Announcement
from server.data import PASSPHRASES, QUESTIONS_STANDARD, QUESTIONS_INTERESTING
from server.logging import get_logger

_LOG = get_logger(__name__)
router = APIRouter()


def get_store(request: Request) -> DataStore:
    return request.app.state.store


def get_targets(request: Request) -> dict:
    return request.app.state.targets


def get_config(request: Request):
    return request.app.state.config


# --- helpers ---

def _met_targets(meetings, student_uuid: str, target_uuids: set) -> set:
    """Return the set of target UUIDs this student has met (either direction)."""
    met = set()
    for m in meetings:
        if m.finder_uuid == student_uuid and m.target_uuid in target_uuids:
            met.add(m.target_uuid)
        elif m.target_uuid == student_uuid and m.finder_uuid in target_uuids:
            met.add(m.finder_uuid)
    return met


def _filter_target_meetings(meetings, student_uuid: str, target_uuids: set) -> list:
    """Return one meeting per met target (deduplicates bidirectional records)."""
    met = _met_targets(meetings, student_uuid, target_uuids)
    seen = set()
    result = []
    for m in meetings:
        other = None
        if m.finder_uuid == student_uuid and m.target_uuid in met:
            other = m.target_uuid
        elif m.target_uuid == student_uuid and m.finder_uuid in met:
            other = m.finder_uuid
        if other and other not in seen:
            seen.add(other)
            result.append(m)
    return result


def _count_target_meetings(meetings, student_uuid: str, target_uuids: set) -> int:
    return len(_met_targets(meetings, student_uuid, target_uuids))


def _pick_questions(n: int) -> list[str]:
    std = random.sample(QUESTIONS_STANDARD, min(1, len(QUESTIONS_STANDARD)))
    inter = random.sample(QUESTIONS_INTERESTING, min(1, len(QUESTIONS_INTERESTING)))
    pool = QUESTIONS_STANDARD + QUESTIONS_INTERESTING
    remaining = random.sample(
        [q for q in pool if q not in std and q not in inter],
        max(0, n - 2),
    )
    questions = std + inter + remaining
    random.shuffle(questions)
    return questions[:n]


def _ordinal(n: int) -> str:
    if 11 <= n % 100 <= 13:
        return f"{n}th"
    return f"{n}{['th','st','nd','rd','th'][min(n % 10, 4)]}"


# --- schemas ---

class RegisterRequest(BaseModel):
    encrypted_id: str
    forename: str
    surname: str


class UpdateNameRequest(BaseModel):
    forename: str
    surname: str


class MeetRequest(BaseModel):
    finder_uuid: str
    passphrase: str


class AnswerRequest(BaseModel):
    finder_uuid: str
    target_uuid: str
    answers: list[dict]


# --- endpoints ---

@router.post("/register")
def register(req: RegisterRequest, store: DataStore = Depends(get_store)):
    existing = store.get_student_by_enc_id(req.encrypted_id)
    if existing:
        name_differs = (
            existing.forename != req.forename or existing.surname != req.surname
        )
        return {
            "uuid": existing.uuid,
            "passphrase": existing.passphrase,
            "is_new": False,
            "forename": existing.forename,
            "surname": existing.surname,
            "name_differs": name_differs,
        }

    student = store.register_new_student(
        uuid=str(_uuid.uuid4()),
        student_id_enc=req.encrypted_id,
        forename=req.forename,
        surname=req.surname,
        passphrase_pool=PASSPHRASES,
    )
    _LOG.info("Registered %s %s (uuid=%s)", req.forename, req.surname, student.uuid)
    return {
        "uuid": student.uuid,
        "passphrase": student.passphrase,
        "is_new": True,
    }


@router.put("/student/{student_uuid}")
def update_name(
    student_uuid: str,
    req: UpdateNameRequest,
    store: DataStore = Depends(get_store),
):
    s = store.get_student_by_uuid(student_uuid)
    if not s:
        raise HTTPException(404, "Student not found")
    store.update_student_name(student_uuid, req.forename, req.surname)
    return {"ok": True}


@router.get("/targets/{student_uuid}")
def get_targets_for_student(
    student_uuid: str,
    request: Request,
    store: DataStore = Depends(get_store),
):
    targets: dict = request.app.state.targets
    if student_uuid not in targets:
        raise HTTPException(404, "Targets not yet assigned")

    result = []
    for t_uuid in targets[student_uuid]:
        t = store.get_student_by_uuid(t_uuid)
        if t:
            hint = t.passphrase.split("-")[0]
            result.append({"forename": t.forename, "surname": t.surname, "passphrase_hint": hint, "uuid": t_uuid})
    return {"targets": result, "assigned": True}


@router.post("/meet")
def meet(
    req: MeetRequest,
    request: Request,
    store: DataStore = Depends(get_store),
):
    targets: dict = request.app.state.targets
    cfg = request.app.state.config

    finder = store.get_student_by_uuid(req.finder_uuid)
    if not finder:
        raise HTTPException(404, "Finder not found")

    target = store.get_student_by_passphrase(req.passphrase)
    if not target:
        return {"ok": False, "reason": "passphrase not recognized"}

    if target.uuid == req.finder_uuid:
        return {"ok": False, "reason": "that is your own passphrase"}

    # passphrase must belong to one of the finder's assigned targets
    assigned = targets.get(req.finder_uuid, [])
    if target.uuid not in assigned:
        return {"ok": False, "reason": "that person is not one of your targets"}

    if store.meeting_exists(req.finder_uuid, target.uuid):
        return {"ok": False, "reason": "you have already met this person"}

    questions = _pick_questions(cfg.questions_per_meeting)
    _LOG.info("%s meeting %s", req.finder_uuid, target.uuid)

    # Notify the target that someone found them so they can auto-navigate.
    lock = request.app.state.pending_meets_lock
    pending = request.app.state.pending_meets
    with lock:
        pending[target.uuid] = {
            "finder_uuid": req.finder_uuid,
            "finder_forename": finder.forename,
            "questions": questions,
        }

    return {
        "ok": True,
        "target_uuid": target.uuid,
        "target_forename": target.forename,
        "questions": questions,
    }


@router.post("/answer")
def answer(
    req: AnswerRequest,
    request: Request,
    store: DataStore = Depends(get_store),
):
    targets: dict = request.app.state.targets
    cfg = request.app.state.config

    meeting = Meeting(
        uuid=str(_uuid.uuid4()),
        finder_uuid=req.finder_uuid,
        target_uuid=req.target_uuid,
        met_at=time.time(),
        answers=json.dumps(req.answers),
    )
    inserted = store.add_meeting_if_not_exists(meeting)
    if not inserted:
        raise HTTPException(409, "Meeting already recorded")

    my_targets = set(targets.get(req.finder_uuid, []))
    all_meetings = store.meetings_for_student(req.finder_uuid)
    completed = _count_target_meetings(all_meetings, req.finder_uuid, my_targets)
    total = len(my_targets)
    _LOG.info("Meeting recorded: %s met %s (%d/%d)", req.finder_uuid, req.target_uuid, completed, total)
    return {"ok": True, "meetings_completed": completed, "total_targets": total}


@router.get("/stats/{student_uuid}")
def stats(
    student_uuid: str,
    request: Request,
    store: DataStore = Depends(get_store),
):
    targets: dict = request.app.state.targets
    cfg = request.app.state.config

    my_target_uuids = set(targets.get(student_uuid, []))
    all_meetings = store.meetings_for_student(student_uuid)
    relevant = _filter_target_meetings(all_meetings, student_uuid, my_target_uuids)
    completed = len(relevant)
    total = len(my_target_uuids)

    finished_at = max((m.met_at for m in relevant), default=None) if completed == total and total > 0 else None

    # compute finish place among those who completed all their targets
    place = None
    ordinal = None
    if finished_at is not None:
        all_finished = store.all_finished_students()
        full_finishers = []
        for u, last_met in all_finished:
            u_targets = set(targets.get(u, []))
            if not u_targets:
                continue
            u_meetings = store.meetings_for_student(u)
            u_completed = _count_target_meetings(u_meetings, u, u_targets)
            if u_completed == len(u_targets):
                full_finishers.append((u, last_met))
        full_finishers.sort(key=lambda x: x[1])
        place = next((i + 1 for i, (u, _) in enumerate(full_finishers) if u == student_uuid), None)
        if place:
            ordinal = _ordinal(place)

    met_uuids = list(_met_targets(all_meetings, student_uuid, my_target_uuids))
    return {
        "meetings_completed": completed,
        "total_targets": total,
        "met_target_uuids": met_uuids,
        "finish_place": place,
        "finish_ordinal": ordinal,
        "finished_at": finished_at,
    }


@router.get("/pending_meet/{student_uuid}")
def pending_meet(student_uuid: str, request: Request):
    """Poll by the target to detect when someone has entered their passphrase."""
    lock = request.app.state.pending_meets_lock
    meets = request.app.state.pending_meets
    with lock:
        pm = meets.pop(student_uuid, None)
    if pm is None:
        return {"pending": False}
    return {"pending": True, **pm}


@router.get("/time")
def server_time(request: Request, store: DataStore = Depends(get_store)):
    now = time.time()
    deadline_str = store.get_state("deadline")
    deadline = float(deadline_str) if deadline_str else None
    remaining = max(0.0, deadline - now) if deadline else None
    return {"server_time": now, "deadline": deadline, "remaining_seconds": remaining}


@router.get("/announcements")
def announcements(
    since: float = 0.0,
    store: DataStore = Depends(get_store),
):
    anns = store.announcements_since(since)
    return {
        "announcements": [
            {"uuid": a.uuid, "message": a.message, "sent_at": a.sent_at}
            for a in anns
        ]
    }
