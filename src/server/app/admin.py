"""Admin-only FastAPI routes. All require Authorization: Bearer <admin_token>."""

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


class AnnounceRequest(BaseModel):
    message: str


class TimeRequest(BaseModel):
    deadline: float | None = None
    add_minutes: float | None = None


class AssignRequest(BaseModel):
    force: bool = False


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
    # remove from in-memory targets
    targets.pop(student_uuid, None)
    for k in targets:
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
        return {"ok": False, "reason": "targets already assigned; use force=true to re-assign"}

    students = store.all_students()
    uuids = [s.uuid for s in students]
    new_targets = assign_targets(uuids, cfg.targets_per_student)
    targets.clear()
    targets.update(new_targets)

    # persist
    import json, pathlib
    pathlib.Path(cfg.targets_file).write_text(json.dumps(targets))
    _LOG.info("Admin assigned targets for %d students", len(students))
    return {"ok": True, "students": len(students)}


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
