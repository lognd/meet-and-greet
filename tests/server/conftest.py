"""Shared fixtures for all server tests."""

import time
import pytest
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.testclient import TestClient

from server.app.config import AppConfig
from server.app.routes import router as student_router
from server.app.admin import router as admin_router
from server.db.store import DataStore
from server.db.models import Student

TEST_KEY   = "deadbeefcafebabedeadbeefcafebabe"
TEST_TOKEN = "test-admin-token"


def make_config(**overrides) -> AppConfig:
    defaults = dict(
        port=19876,
        udp_port=19875,
        encryption_key=TEST_KEY,
        admin_token=TEST_TOKEN,
        time_limit_minutes=0,
        targets_per_student=2,
        db_file=":memory:",
        targets_file="/tmp/mag_test_targets.json",
        log_file="/tmp/mag_test.log",
        questions_per_meeting=2,
    )
    defaults.update(overrides)
    return AppConfig(**defaults)


@pytest.fixture()
def cfg() -> AppConfig:
    return make_config()


@pytest.fixture()
def store() -> DataStore:
    return DataStore(":memory:")


@pytest.fixture()
def populated_store(store: DataStore) -> DataStore:
    store.add_student(Student(
        uuid="uuid-alice",
        student_id_enc="enc-alice",
        forename="Alice",
        surname="Alpha",
        passphrase="eagle-has-landed",
        registered_at=time.time(),
    ))
    store.add_student(Student(
        uuid="uuid-bob",
        student_id_enc="enc-bob",
        forename="Bob",
        surname="Beta",
        passphrase="fox-in-the-henhouse",
        registered_at=time.time() + 1,
    ))
    return store


def _build_app(cfg: AppConfig, store: DataStore, targets: dict) -> FastAPI:
    @asynccontextmanager
    async def lifespan(app):
        yield

    app = FastAPI(lifespan=lifespan)
    app.state.store   = store
    app.state.targets = targets
    app.state.config  = cfg
    app.include_router(student_router)
    app.include_router(admin_router)
    return app


@pytest.fixture()
def client(cfg, store):
    return TestClient(_build_app(cfg, store, {}), raise_server_exceptions=True)


@pytest.fixture()
def client_with_targets(cfg, populated_store):
    targets = {"uuid-alice": ["uuid-bob"], "uuid-bob": ["uuid-alice"]}
    return TestClient(
        _build_app(cfg, populated_store, targets),
        raise_server_exceptions=True,
    )
