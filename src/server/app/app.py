import json
import time
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
from fastapi import FastAPI

from server.app.config import AppConfig
from server.app.routes import router as student_router
from server.app.admin import router as admin_router
from server.db import DataStore
from server.discovery import UDPDiscovery
from server.logging import get_logger

_LOG = get_logger(__name__)


class App:
    def __init__(self, cfg: AppConfig) -> None:
        self._cfg = cfg
        self._store = DataStore(cfg.db_file)
        self._targets: dict[str, list[str]] = self._load_targets()
        self._discovery = UDPDiscovery(cfg.udp_port, cfg.port)

        @asynccontextmanager
        async def lifespan(app: FastAPI):
            self._on_startup()
            yield
            self._on_shutdown()

        self._api = FastAPI(title="Meet and Greet", version="1.0.0", lifespan=lifespan)
        self._api.state.store = self._store
        self._api.state.targets = self._targets
        self._api.state.config = cfg
        self._api.include_router(student_router)
        self._api.include_router(admin_router)

    def _load_targets(self) -> dict[str, list[str]]:
        p = Path(self._cfg.targets_file)
        if p.exists():
            try:
                return json.loads(p.read_text())
            except Exception:
                pass
        return {}

    def _on_startup(self) -> None:
        self._discovery.start()
        if self._cfg.time_limit_minutes > 0:
            deadline = time.time() + self._cfg.time_limit_minutes * 60
            self._store.set_state("deadline", str(deadline))
            _LOG.info("Session deadline set: %.0f seconds from now", self._cfg.time_limit_minutes * 60)

    def _on_shutdown(self) -> None:
        self._discovery.stop()
        self._store.close()

    def __call__(self) -> None:
        _LOG.info("Starting MAG server on port %d", self._cfg.port)
        uvicorn.run(self._api, host="0.0.0.0", port=self._cfg.port, log_level="warning")
