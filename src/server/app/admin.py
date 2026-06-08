"""Admin-only FastAPI routes. All require Authorization: Bearer <admin_token>."""

import json
import os
import pathlib
import signal
import threading
import time
import uuid as _uuid

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from pydantic import BaseModel

from server.db import DataStore, Announcement
from server.graph import assign_targets
from server.logging import get_logger

_LOG = get_logger(__name__)
router = APIRouter(prefix="/admin")
_bearer = HTTPBearer()


def _check_auth(
    request: Request,
    creds: HTTPAuthorizationCredentials = Depends(_bearer),
) -> None:
    expected = request.app.state.config.admin_token
    if creds.credentials != expected:
        raise HTTPException(401, "Invalid admin token")


def get_store(request: Request) -> DataStore:
    return request.app.state.store


# ---------------------------------------------------------------------------
# Auto-exit helper
# ---------------------------------------------------------------------------

def check_all_done(app) -> bool:
    """Return True if every non-phantom student with assigned targets has met
    all of them.  When True on the first call, schedules a graceful SIGTERM
    after a 30-second grace period so clients can view their stats screen.

    Safe to call from any route handler (holds no locks for long).
    """
    targets: dict = app.state.targets
    phantom_uuids: frozenset = getattr(app.state, "phantom_uuids", frozenset())
    store: DataStore = app.state.store

    real_targets = {u: vs for u, vs in targets.items() if u not in phantom_uuids}
    if not real_targets:
        return False

    for student_uuid, target_list in real_targets.items():
        if not target_list:
            continue
        my_targets = set(target_list)
        meetings = store.meetings_for_student(student_uuid)
        met: set[str] = set()
        for m in meetings:
            if m.finder_uuid == student_uuid and m.target_uuid in my_targets:
                met.add(m.target_uuid)
            elif m.target_uuid == student_uuid and m.finder_uuid in my_targets:
                met.add(m.finder_uuid)
        if len(met) < len(my_targets):
            return False

    # All real students finished.
    if not getattr(app.state, "shutdown_scheduled", False):
        app.state.shutdown_scheduled = True
        _LOG.info("All students finished - shutting down in 30 s")

        def _shutdown():
            _LOG.info("Auto-shutdown triggered")
            os.kill(os.getpid(), signal.SIGTERM)

        threading.Timer(30.0, _shutdown).start()

    return True


# ---------------------------------------------------------------------------
# Schemas
# ---------------------------------------------------------------------------

class AnnounceRequest(BaseModel):
    message: str


class TimeRequest(BaseModel):
    deadline: float | None = None
    add_minutes: float | None = None


class AssignRequest(BaseModel):
    force: bool = False
    # UUIDs of phantom students registered via master mode.
    # The CLI reads master_state.json and populates this automatically.
    phantom_uuids: list[str] = []


# ---------------------------------------------------------------------------
# Endpoints
# ---------------------------------------------------------------------------

@router.get("/students", dependencies=[Depends(_check_auth)])
def list_students(store: DataStore = Depends(get_store)):
    students = store.all_students()
    return {
        "students": [
            {
                "uuid": s.uuid,
                "forename": s.forename,
                "surname": s.surname,
                "passphrase": s.passphrase,
                "registered_at": s.registered_at,
            }
            for s in students
        ],
        "count": len(students),
    }


@router.delete("/student/{student_uuid}", dependencies=[Depends(_check_auth)])
def delete_student(
    student_uuid: str,
    request: Request,
    store: DataStore = Depends(get_store),
):
    targets: dict = request.app.state.targets
    ok = store.delete_student(student_uuid)
    if not ok:
        raise HTTPException(404, "Student not found")
    targets.pop(student_uuid, None)
    for k in list(targets):
        if student_uuid in targets[k]:
            targets[k].remove(student_uuid)
    _LOG.info("Admin deleted student %s", student_uuid)
    return {"ok": True}


@router.post("/assign", dependencies=[Depends(_check_auth)])
def admin_assign(
    req: AssignRequest,
    request: Request,
    store: DataStore = Depends(get_store),
):
    targets: dict = request.app.state.targets
    cfg = request.app.state.config

    if targets and not req.force:
        return {
            "ok": False,
            "reason": "targets already assigned; use force=true to re-assign",
        }

    ph_set = frozenset(req.phantom_uuids)
    # Persist phantom UUIDs in app state for the all-done check.
    request.app.state.phantom_uuids = ph_set

    students = store.all_students()
    uuids = [s.uuid for s in students]
    new_targets = assign_targets(uuids, cfg.targets_per_student, ph_set)
    targets.clear()
    targets.update(new_targets)

    pathlib.Path(cfg.targets_file).write_text(json.dumps(targets))

    n_real    = sum(1 for u in uuids if u not in ph_set)
    n_phantom = len(ph_set)
    _LOG.info(
        "Admin assigned targets: %d real, %d phantom students",
        n_real, n_phantom,
    )
    return {
        "ok": True,
        "students": len(students),
        "real": n_real,
        "phantoms": n_phantom,
    }


@router.post("/announce", dependencies=[Depends(_check_auth)])
def announce(req: AnnounceRequest, store: DataStore = Depends(get_store)):
    ann = Announcement(
        uuid=str(_uuid.uuid4()),
        message=req.message,
        sent_at=time.time(),
    )
    store.add_announcement(ann)
    _LOG.info("Admin announcement: %s", req.message)
    return {"ok": True, "uuid": ann.uuid}


@router.post("/time", dependencies=[Depends(_check_auth)])
def set_time(req: TimeRequest, store: DataStore = Depends(get_store)):
    if req.deadline is not None:
        deadline = req.deadline
    elif req.add_minutes is not None:
        now_str = store.get_state("deadline")
        base = float(now_str) if now_str else time.time()
        deadline = base + req.add_minutes * 60
    else:
        raise HTTPException(400, "Provide deadline or add_minutes")
    store.set_state("deadline", str(deadline))
    return {"ok": True, "deadline": deadline}
