"""Entry points:

    python -m server            -> start server (reads $MAG_TOML or app.toml)
    python -m server admin ...  -> admin CLI subcommands
"""

import sys
import os
from pathlib import Path


def cli_main() -> None:
    """Installed as the `mag` console script."""
    if len(sys.argv) > 1 and sys.argv[1] == "admin":
        sys.argv = [sys.argv[0]] + sys.argv[2:]
        from server.cli import admin_app
        admin_app()
    else:
        _run_server()


def _run_server() -> None:
    from server.app import App, AppConfig
    toml = os.environ.get("MAG_TOML", "app.toml")
    cfg = AppConfig.from_toml(Path(toml))
    app = App(cfg)
    app()


if __name__ == "__main__":
    cli_main()
