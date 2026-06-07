# Meet and Greet (MAG)

A classroom icebreaker system. Students find their assigned peers using
spy-style passphrases, answer a few get-to-know-you questions, and race to
complete all their meetings before the clock runs out.

```
Instructor runs: python -m server          (Python, any machine on the LAN)
Students run:    mag_client                (compiled C++ binary, Windows / Linux / macOS)
```

---

## Contents

1. [How it works](#how-it-works)
2. [Quick start](#quick-start)
3. [Building the client](#building-the-client)
4. [Server configuration](#server-configuration)
5. [Running a session](#running-a-session)
6. [Admin CLI reference](#admin-cli-reference)
7. [API reference](#api-reference)
8. [Cross-platform release builds](#cross-platform-release-builds)
9. [Developer guide](#developer-guide)

---

## How it works

1. The instructor starts the server on their laptop. It broadcasts its IP over
   UDP so students do not need to type an address.
2. Students launch `mag_client`. The TUI discovers the server automatically,
   then asks for their name and student ID. The ID is XOR-obfuscated before
   it leaves the device; the server never stores raw IDs.
3. Each student receives a secret spy passphrase (e.g. `eagle-has-landed`).
4. The instructor triggers target assignment. Each student is given a short list
   of peers to find. Target lists form a k-regular graph so every student has
   the same number of meetings.
5. Students wander the room. When they find a target they exchange passphrases,
   answer questions together, and both clients mark the meeting complete.
6. The first student to finish all meetings wins. Finish places are displayed in
   the TUI.

---

## Quick start

### Requirements

| Component | Requirement |
|-----------|-------------|
| Python server | Python >= 3.10, pip |
| C++ client | CMake >= 3.22, C++20 compiler, internet (first build only) |
| OS (server) | Linux, macOS, Windows (WSL also works) |
| OS (client) | Linux, macOS, Windows |

### 1. Set up the Python server

```bash
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install tomli                  # Python < 3.11 only
pip install -e .
```

Copy and edit the config:

```bash
cp app.toml.example app.toml
```

At minimum change `admin_token` before starting. See
[Server configuration](#server-configuration) for all options.

### 2. Build the C++ client

```bash
make build
# or manually:
cmake -B build -DMAG_XORKEY="<32-char hex matching app.toml encryption_key>"
cmake --build build -j$(nproc)
```

The binary is `build/mag_client` (Linux/macOS) or `build/mag_client.exe`
(Windows). Distribute it to students.

### 3. Run a session

```bash
# Terminal 1 - start server
python -m server

# Terminal 2 - watch logs / run admin commands
python -m server admin students
```

Students double-click `mag_client` (or run it from a terminal). The TUI
handles everything else.

---

## Building the client

### Variables

| CMake variable | Default | Description |
|----------------|---------|-------------|
| `MAG_XORKEY` | `deadbeefcafebabe...` | 32-char hex key (must match `encryption_key` in `app.toml`) |
| `MAG_HTTP_PORT` | `9876` | HTTP port baked into the binary |
| `MAG_UDP_PORT` | `9875` | UDP discovery port |

### Dependencies (fetched automatically)

| Library | Version | Purpose |
|---------|---------|---------|
| cpp-httplib | v0.18.7 | HTTP client |
| nlohmann/json | v3.11.3 | JSON parsing |
| FTXUI | v6.1.9 | Cross-platform terminal UI |
| Catch2 | v3.5.3 | C++ unit tests |

All dependencies are downloaded by CMake FetchContent on the first build. A
network connection is required only for that initial fetch.

### Debug vs release

```bash
# Release (smaller, faster)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMAG_XORKEY="<key>"
cmake --build build -j$(nproc)

# Debug (adds -g, no optimisations)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DMAG_XORKEY="<key>"
cmake --build build -j$(nproc)
```

---

## Server configuration

All options live in `app.toml` (or the file pointed to by the `MAG_TOML`
environment variable).

```toml
# Network
port                  = 9876       # HTTP API port
udp_port              = 9875       # UDP discovery port

# Security
encryption_key        = "deadbeefcafebabedeadbeefcafebabe"  # 32-char hex
admin_token           = "change-me-before-use"

# Session
time_limit_minutes    = 20         # 0 = no automatic deadline
targets_per_student   = 5          # edges per node in the k-regular graph

# Storage
db_file               = "mag.db"
targets_file          = "targets.json"
log_file              = "mag.log"

# Behaviour
questions_per_meeting = 3
```

### Choosing `encryption_key`

The key is baked into the compiled client binary at build time. A student's
numeric ID is XOR-obfuscated with this key before being sent over the network.
This prevents casual interception of raw IDs on the LAN; it is not
cryptographic security.

**Change the key before each semester.** This ensures last year's binaries
cannot register in this year's session.

```bash
# Generate a new key (Linux/macOS)
python3 -c "import secrets; print(secrets.token_hex(16))"
```

Then update both `app.toml` and rebuild the client.

### Choosing `targets_per_student`

MAG builds a k-regular graph. For n students and k targets:

- k must be < n
- If n * k is odd, k is automatically reduced by 1 for that session
- If n <= k, a complete graph is used (everyone meets everyone)

Typical classroom values: k=3 for a 10-minute session, k=5 for 20 minutes.

---

## Running a session

### Full run sequence

```
1. Start server            python -m server
2. Students register       (they open mag_client; this is automatic)
3. Assign targets          python -m server admin assign
4. Session runs            students wander and meet each other
5. Monitor progress        python -m server admin students
6. Close session           Ctrl-C the server
```

### Step by step

#### Start the server

```bash
MAG_TOML=app.toml python -m server
```

The server logs to the console and to `mag.log`. It starts broadcasting its
IP on UDP immediately so clients can discover it.

#### Wait for students to register

Students open `mag_client`. The TUI will:

1. Discover the server (UDP broadcast, ~5 s)
2. Ask for name and student ID
3. Display their passphrase
4. Show a waiting screen until targets are assigned

You can see who has registered:

```bash
python -m server admin students
```

#### Assign targets

Once all students are registered (or at any point you choose):

```bash
python -m server admin assign
```

All waiting client screens will refresh automatically and show the list of
people to find.

#### During the session

Send announcements that appear as overlays on all client screens:

```bash
python -m server admin announce "Halfway done! Great work."
```

Adjust the deadline on the fly:

```bash
python -m server admin time --add-minutes 5   # extend by 5 minutes
python -m server admin time --deadline 1720000000  # set absolute Unix timestamp
```

Remove a student who left early:

```bash
python -m server admin delete <uuid>
```

View leaderboard:

```bash
python -m server admin stats
```

#### Reconnecting

If a student's laptop crashes or they close the client, they can relaunch
`mag_client` and enter the same student ID. The server recognises the
obfuscated ID and restores their session (passphrase, completed meetings,
targets) without any data loss.

---

## Admin CLI reference

All admin commands require the server to be running.

```
python -m server admin <command> [options]
```

| Command | Description |
|---------|-------------|
| `students` | List all registered students |
| `assign` | Assign targets (k-regular graph) |
| `assign --force` | Re-assign targets even if already assigned |
| `announce <message>` | Broadcast a message to all clients |
| `time --add-minutes N` | Extend deadline by N minutes |
| `time --deadline TS` | Set deadline to Unix timestamp TS |
| `delete <uuid>` | Remove a student |
| `stats` | Show completion leaderboard |

Example:

```bash
python -m server admin assign
python -m server admin announce "The hunt begins! Good luck."
python -m server admin time --add-minutes 10
python -m server admin stats
```

---

## API reference

The server exposes a plain HTTP JSON API. No authentication is required for
student endpoints. All admin endpoints require `Authorization: Bearer <admin_token>`.

### Student endpoints

#### `POST /register`

Register or reconnect a student.

Request:
```json
{
  "encrypted_id": "<hex string>",
  "forename": "Alice",
  "surname": "Smith"
}
```

Response:
```json
{
  "uuid": "<uuid4>",
  "passphrase": "eagle-has-landed",
  "is_new": true,
  "name_differs": false
}
```

`is_new` is `false` on reconnect. `name_differs` is `true` if the stored name
does not match the supplied name (prompts the client to offer a name update).

---

#### `PUT /student/{uuid}`

Update a student's display name.

Request:
```json
{ "forename": "Alice", "surname": "Smith" }
```

Response: `{ "ok": true }`

---

#### `GET /targets/{uuid}`

Get the list of targets assigned to a student.

Returns 404 until `admin assign` has been run.

Response:
```json
{
  "targets": [
    {
      "uuid": "<uuid>",
      "forename": "Bob",
      "surname": "Jones",
      "passphrase_hint": "fox"
    }
  ],
  "assigned": true
}
```

`passphrase_hint` is the first word of the target's passphrase. It helps
students narrow down who they are looking for without revealing the full
passphrase needed to confirm the meeting.

---

#### `POST /meet`

Confirm a meeting. The finder submits the target's full passphrase.

Request:
```json
{
  "finder_uuid": "<uuid>",
  "passphrase": "fox-in-the-henhouse"
}
```

Success response:
```json
{
  "ok": true,
  "target_uuid": "<uuid>",
  "target_forename": "Bob",
  "questions": ["What is your favourite travel memory?", "..."]
}
```

Failure response (passphrase wrong / not a target / already met):
```json
{ "ok": false, "reason": "that person is not one of your targets" }
```

---

#### `POST /answer`

Submit answers to meeting questions and record the meeting.

Request:
```json
{
  "finder_uuid": "<uuid>",
  "target_uuid": "<uuid>",
  "answers": [
    { "question": "What is your favourite travel memory?", "answer": "..." }
  ]
}
```

Response:
```json
{ "ok": true, "meetings_completed": 2, "total_targets": 5 }
```

Returns 409 if the meeting was already recorded.

---

#### `GET /stats/{uuid}`

Get a student's completion stats.

Response:
```json
{
  "meetings_completed": 5,
  "total_targets": 5,
  "finish_place": 3,
  "finish_ordinal": "3rd",
  "finished_at": 1720000042.5
}
```

`finish_place` and `finish_ordinal` are `null` until the student completes all
meetings.

---

#### `GET /time`

Get the server clock and deadline.

Response:
```json
{
  "server_time": 1720000000.0,
  "deadline": 1720001200.0,
  "remaining_seconds": 1200.0
}
```

`deadline` and `remaining_seconds` are `null` when no deadline is set.

---

#### `GET /announcements?since=<timestamp>`

Poll for announcements sent after `since` (Unix timestamp, default 0).

Response:
```json
{
  "announcements": [
    { "uuid": "<uuid>", "message": "Time is almost up!", "sent_at": 1720000900.0 }
  ]
}
```

---

### Admin endpoints

All require `Authorization: Bearer <admin_token>`.

| Method | Path | Body | Description |
|--------|------|------|-------------|
| `GET` | `/admin/students` | - | List all students |
| `DELETE` | `/admin/student/{uuid}` | - | Remove a student |
| `POST` | `/admin/assign` | `{"force": false}` | Assign/re-assign targets |
| `POST` | `/admin/announce` | `{"message": "..."}` | Broadcast announcement |
| `POST` | `/admin/time` | `{"add_minutes": 5}` or `{"deadline": 1720001200}` | Set deadline |

---

## Cross-platform release builds

`make release` produces ready-to-distribute binaries for all common targets.
It uses Docker + a MinGW cross-compiler for Windows and a sysroot for macOS.

```bash
make release                       # builds all three platforms
make release-linux                 # Linux x86-64 only
make release-windows               # Windows x86-64 only (cross-compiled with MinGW)
make release-macos                 # macOS universal binary (arm64 + x86-64, requires macOS host)
```

Binaries are placed in `dist/`:

```
dist/
  mag_client-linux-x86_64
  mag_client-windows-x86_64.exe
  mag_client-macos-universal
```

### Requirements

| Target | Host requirement |
|--------|-----------------|
| Linux x86-64 | Any Linux x86-64 host with Docker |
| Windows x86-64 | Any Linux x86-64 host with Docker + MinGW (`mingw-w64`) |
| macOS universal | macOS host with Xcode Command Line Tools and CMake |

### Changing the encryption key for distribution

Always bake the key that matches your `app.toml` into the binaries:

```bash
make release XORKEY="$(grep encryption_key app.toml | cut -d'"' -f2)"
```

---

## Developer guide

### Repository layout

```
meet-and-greet/
  app.toml.example      server config template
  CMakeLists.txt        C++ build (client + tests)
  Makefile              top-level build / test targets
  pyproject.toml        Python package manifest
  pytest.ini            test runner config

  src/
    server/             Python FastAPI server
      __main__.py       entry point (server or admin CLI)
      app/
        app.py          FastAPI application + lifespan
        config.py       AppConfig Pydantic model
        routes.py       student-facing endpoints
        admin.py        admin-only endpoints
      cli/
        admin.py        Typer CLI (python -m server admin ...)
      crypto/
        cipher.py       XOR obfuscation helpers
      data/
        passphrases.py  spy passphrase bank (131 phrases)
        questions.py    question banks (standard + interesting)
      db/
        models.py       Pydantic DB models (Student, Meeting, Announcement)
        store.py        SQLite data store (thread-safe with Lock)
      discovery/
        udp.py          UDP broadcast / listener daemon threads
      graph/
        targets.py      k-regular graph assignment algorithm
      logging/
        logger.py       logging config loader
        logger.toml     handler / formatter / logger config

    client/             C++20 client
      main.cpp          entry point (TUI or headless mode)
      net/
        http.cpp        HttpClient (cpp-httplib wrapper)
        udp.cpp         UDP discovery (platform-conditional)
      tui/
        screens.cpp     FTXUI screens (discover, register, wait, hunt, stats)
        master.cpp      instructor master-mode screen

  include/client/       C++ header-only interfaces
    data.h              structs + XOR cipher
    network.h           HttpClient + discover_server declarations
    tui.h               TUI screen function declarations

  tests/
    server/             pytest unit + integration tests
      conftest.py       TestClient fixtures, shared config
      test_admin.py     admin endpoint tests
      test_cipher.py    XOR cipher round-trip tests
      test_config.py    AppConfig loading tests
      test_data.py      passphrase / question bank tests
      test_graph.py     k-regular graph tests
      test_models.py    Pydantic model tests
      test_routes.py    student endpoint integration tests
      test_store.py     DataStore unit tests
      test_system.py    real server subprocess tests
    client/             Catch2 C++ tests
      test_data.cpp     data struct + cipher tests
      test_network.cpp  HttpClient integration tests (mock server)
    system/             full system tests
      harness.py        ServerProcess + SimStudent helpers
      conftest.py       platform markers
      test_binary.py    headless client binary tests
      test_scale.py     120-student scale + edge-case tests

  docs/
    design/README.md    full architecture and protocol specification
```

### Running tests

```bash
make test               # all tests (Python + C++)
make test-python        # server unit/integration + system tests
make test-cpp           # C++ unit + integration tests (ctest)
make test-fast          # only server unit/integration (no subprocess overhead)
make test-system        # full system + scale tests
make test-binary        # headless binary tests (requires: make build first)
make test-scale         # 120-student test only
```

The system tests start real server subprocesses and use isolated temporary
directories. They take about 20-30 s on a modern laptop.

Binary tests (`test_binary.py`) are marked `@posix_only` and
`@requires_binary`. They are automatically skipped if the binary is not built
or the OS is Windows.

### Adding tests

Server tests live in `tests/server/`. They use `TestClient` from Starlette
and never start a real subprocess (fast). See `tests/server/conftest.py` for
the fixture set.

System tests live in `tests/system/`. Use `ServerProcess` as a context
manager to get an isolated server, and `SimStudent` to drive HTTP interactions:

```python
from tests.system.harness import ServerProcess, SimStudent

def test_something():
    with ServerProcess(port=19899, udp_port=19900) as srv:
        s = SimStudent(123456, "Alice", "Smith", srv)
        s.register()
        srv.assign_targets()
        targets = s.get_targets()
        ...
```

Pick port numbers that are not used by any other test module to avoid
conflicts when tests run in parallel.

### Database

The server uses SQLite with WAL journal mode. A single `DataStore` instance
is shared across all FastAPI threadpool workers. A `threading.Lock` serialises
every connection access because Python's `sqlite3.Connection` is not
thread-safe even with `check_same_thread=False`.

Critical race conditions that required atomic operations:

- **Passphrase assignment**: `register_new_student()` reads used passphrases,
  picks an available one, and inserts the student record under one lock.
  Without this, concurrent registrations can pick the same passphrase.

- **Meeting recording**: `add_meeting_if_not_exists()` checks for an existing
  meeting (in either direction) and inserts under one lock. Without this,
  two concurrent `/answer` calls for the same pair can both succeed, creating
  duplicate records that inflate meeting counts.

### Encryption

Student IDs are obfuscated with XOR before leaving the device. The same logic
runs in Python (server) and C++ (client):

```python
# Python
def encrypt_id(student_id: int, key_hex: str) -> str:
    key = bytes.fromhex(key_hex)
    plain = str(student_id).encode()
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(plain)).hex()
```

```cpp
// C++ (compile-time key via MAG_XORKEY define)
inline std::string encrypt_id(uint64_t id) {
    constexpr std::string_view key_hex = MAG_XORKEY;
    // ... same XOR logic
}
```

This is obfuscation, not encryption. A determined attacker with both a
compiled binary and a network capture can recover raw IDs. It is intended
only to prevent casual interception on an open LAN.

### Target graph algorithm

Targets are assigned as a k-regular graph via circular shift:

```python
# Pseudocode
for i, uuid in enumerate(uuids):
    targets[uuid] = [uuids[(i + j) % n] for j in range(1, k + 1)]
```

Edge cases:

- **n <= k**: complete graph (everyone meets everyone).
- **n * k is odd**: k is reduced by 1 (a regular graph requires an even sum
  of degrees).

### UDP discovery

The server runs two daemon threads:

- **Broadcaster**: sends `MAG_SERVER <ip> <port>` to `255.255.255.255` every
  5 s on the UDP port.
- **Listener**: responds to `MAG_WHO` unicast with `MAG_SERVER <ip> <port>`.

The C++ client sends a broadcast `MAG_WHO` and listens for the response. On
networks that block broadcast, students can bypass discovery with
`--server ip:port` (available in headless mode and on the command line).

### Headless client mode

The client binary accepts `--headless <student_id>` for scripted use and
system testing:

```
mag_client --headless 123456 --server 127.0.0.1:9876
```

Output is line-oriented `KEY=VALUE`:

```
UUID=<uuid4>
PASS=eagle-has-landed
NEW=1
```

Exit codes:

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Server not found (UDP timeout) |
| 2 | Registration failed |
| 3 | Bad arguments |

### Logging

The server uses Python's standard `logging` with a TOML-based config at
`src/server/logging/logger.toml`. Two handlers are configured by default:

- **console**: INFO level, colourised via `rich`
- **file**: DEBUG level, rotating (10 MB per file, 3 backups)

Override the log file path in `app.toml` (`log_file` key).

### Code style

- Python: no type: ignore comments; Pydantic v2 models for all data transfer;
  FastAPI dependency injection for store / config / targets.
- C++20: `std::optional`, structured bindings, `using namespace` avoided in
  headers.
- No comments that describe *what* the code does; only comments for non-obvious
  *why* (invariants, workarounds, subtle constraints).

### Making a new release

1. Update the version in `pyproject.toml`.
2. Decide on a new `encryption_key` (see [Choosing encryption_key](#choosing-encryption_key)).
3. Update `app.toml.example` with the new key.
4. `make release XORKEY="<new-key>"` — produces all three platform binaries.
5. Commit, tag, distribute `dist/` to students.

---

## Security notes

- The admin token is sent as a Bearer token in plain HTTP. Use this system only
  on a trusted LAN (classroom Wi-Fi, direct ethernet, etc.).
- Student IDs are XOR-obfuscated, not encrypted. Do not rely on this for FERPA
  or similar privacy compliance.
- Rotate `encryption_key` and `admin_token` before each semester.
- The server has no rate limiting. Malicious students on the LAN can spam the
  API. If that is a concern, put nginx in front with a rate-limit rule.
