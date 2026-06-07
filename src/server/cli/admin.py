"""Admin CLI - hits the HTTP API using the admin token from app.toml.

Usage (from the server machine):
    python -m server admin students
    python -m server admin delete <uuid>
    python -m server admin assign
    python -m server admin announce "Please wrap up!"
    python -m server admin stats
    python -m server admin time 5
"""

import sys
from pathlib import Path

import typer
import urllib.request
import urllib.error
import json

if sys.version_info >= (3, 11):
    import tomllib
else:
    import tomli as tomllib  # type: ignore[no-redef]

from rich.console import Console
from rich.table import Table

admin_app = typer.Typer(help="MAG admin CLI", no_args_is_help=True)
console = Console()


def _load_cfg(toml: str = "app.toml"):
    with open(toml, "rb") as f:
        return tomllib.load(f)


def _base_url(cfg: dict) -> str:
    return f"http://127.0.0.1:{cfg.get('port', 9876)}"


def _headers(cfg: dict) -> dict:
    return {
        "Authorization": f"Bearer {cfg['admin_token']}",
        "Content-Type": "application/json",
    }


def _request(method: str, url: str, headers: dict, body: dict | None = None) -> dict:
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        console.print(f"[red]HTTP {e.code}:[/red] {e.read().decode()}")
        raise typer.Exit(1)


@admin_app.command("students")
def cmd_students(toml: str = "app.toml"):
    """List all registered students."""
    cfg = _load_cfg(toml)
    data = _request("GET", f"{_base_url(cfg)}/admin/students", _headers(cfg))
    table = Table("UUID", "Forename", "Surname", "Passphrase", title="Students")
    for s in data["students"]:
        table.add_row(s["uuid"][:8] + "...", s["forename"], s["surname"], s["passphrase"])
    console.print(table)
    console.print(f"Total: {data['count']}")


@admin_app.command("delete")
def cmd_delete(uuid: str, toml: str = "app.toml"):
    """Delete a student by UUID."""
    cfg = _load_cfg(toml)
    _request("DELETE", f"{_base_url(cfg)}/admin/student/{uuid}", _headers(cfg))
    console.print(f"[green]Deleted[/green] {uuid}")


@admin_app.command("assign")
def cmd_assign(force: bool = False, toml: str = "app.toml"):
    """Trigger target assignment."""
    cfg = _load_cfg(toml)
    data = _request("POST", f"{_base_url(cfg)}/admin/assign", _headers(cfg), {"force": force})
    if data.get("ok"):
        console.print(f"[green]Assigned targets[/green] for {data['students']} students")
    else:
        console.print(f"[yellow]{data.get('reason')}[/yellow]")


@admin_app.command("announce")
def cmd_announce(message: str, toml: str = "app.toml"):
    """Broadcast a message to all clients."""
    cfg = _load_cfg(toml)
    _request("POST", f"{_base_url(cfg)}/admin/announce", _headers(cfg), {"message": message})
    console.print(f"[green]Sent:[/green] {message}")


@admin_app.command("time")
def cmd_time(add_minutes: float, toml: str = "app.toml"):
    """Add N minutes to the session deadline."""
    cfg = _load_cfg(toml)
    data = _request("POST", f"{_base_url(cfg)}/admin/time", _headers(cfg), {"add_minutes": add_minutes})
    console.print(f"[green]Deadline updated:[/green] {data['deadline']}")


@admin_app.command("stats")
def cmd_stats(toml: str = "app.toml"):
    """Show server time and deadline."""
    cfg = _load_cfg(toml)
    data = _request("GET", f"{_base_url(cfg)}/time", _headers(cfg))
    remaining = data.get("remaining_seconds")
    if remaining is not None:
        mins, secs = divmod(int(remaining), 60)
        console.print(f"Time remaining: [bold]{mins}m {secs}s[/bold]")
    else:
        console.print("No deadline set")
