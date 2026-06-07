import logging
import logging.config
import sys
from pathlib import Path

if sys.version_info >= (3, 11):
    import tomllib
else:
    import tomli as tomllib  # type: ignore[no-redef]

_configured = False

def _configure() -> None:
    global _configured
    if _configured:
        return
    toml_path = Path(__file__).parent / "logger.toml"
    with open(toml_path, "rb") as f:
        cfg = tomllib.load(f)
    logging.config.dictConfig(cfg)
    _configured = True

def get_logger(name: str = "server") -> logging.Logger:
    _configure()
    return logging.getLogger(name)
