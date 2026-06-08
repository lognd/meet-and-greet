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
6. [Phantom students](#phantom-students)
7. [Admin CLI reference](#admin-cli-reference)
8. [API reference](#api-reference)
9. [Cross-platform release builds](#cross-platform-release-builds)
10. [Developer guide](#developer-guide)
11. [Troubleshooting](#troubleshooting)
    - [Windows: SmartScreen blocks the executable](#windows-windows-protected-your-pc-or-nothing-happens-when-double-clicking)
    - [Linux: permission denied](#linux--chromeos-linux-permission-denied-or-nothing-happens)
    - [Stuck on discovery screen](#stuck-on-the-discovery-screen-throbber-spinning-never-connects)

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

# Terminal 2 - admin
python -m server admin students    # see who has registered
python -m server admin assign      # assign targets once everyone is in
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

Generate a new key:

```bash
bash scripts/genkey.sh
```

Then update both `app.toml` and rebuild the client (or push a new release tag
so GitHub Actions rebuilds with the new key).

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
6. Server auto-exits       30 s after the last real student finishes
```

### Start the server

```bash
MAG_TOML=app.toml python -m server
```

The server logs to the console and to `mag.log`. It starts broadcasting its
IP on UDP immediately so clients can discover it.

### Wait for students to register

Students open `mag_client`. The TUI will:

1. Discover the server (UDP broadcast, ~5 s)
2. Ask for name and student ID
3. Display their passphrase
4. Show a waiting screen until targets are assigned

```bash
python -m server admin students    # see who is registered
```

### Assign targets

Once all students are registered (or at any point you choose):

```bash
python -m server admin assign
```

All waiting client screens refresh automatically and show the hunt list. If
you have phantom students set up (see below), run this command from the same
directory as `master_state.json` and phantoms are detected automatically.

### During the session

```bash
python -m server admin announce "Halfway done! Great work."
python -m server admin time 5      # extend deadline by 5 minutes
python -m server admin delete <uuid>   # remove a student who left early
```

### Reconnecting

If a student's laptop crashes or they close the client, they can relaunch
`mag_client` and enter the same student ID. The server recognises the
obfuscated ID and restores their session without data loss.

### Auto-exit

When every real student has finished all their meetings the server waits 30
seconds (so clients can view their stats screen) then exits via SIGTERM.
Phantom students are excluded from this check.

---

## Phantom students

Phantoms are virtual students with no real person behind them. They are useful
when class size does not divide evenly into the target graph, or when you want
to test with fewer real devices.

The server treats phantoms as real students for target assignment purposes, but
excludes them from the completion check. When a real student enters a phantom's
passphrase the meeting is recorded on both sides automatically (symmetric
meeting logic), so the real student's progress advances correctly.

**Guarantee:** no phantom is ever assigned another phantom as a target, because
neither side could initiate the meeting.

### Setting up phantoms

#### Step 1 - start the server

```bash
python -m server
```

#### Step 2 - register real students

Open one terminal per participant and run `mag_client`. Leave these running.

#### Step 3 - register phantom students

On the server machine, run master mode once per phantom:

```bash
./build/mag_client --master --name "Phantom One"
./build/mag_client --master --name "Phantom Two"
```

Master mode registers the student, prints their UUID and passphrase, then
appends to `master_state.json` in the current directory:

```
Registered: Phantom One (uuid=abc123..., passphrase=maple-river-seven)
State written to master_state.json
```

#### Step 4 - assign targets

```bash
python -m server admin assign
```

The CLI reads `master_state.json` automatically:

```
Detected 2 phantom(s) from master_state.json
Assigned targets for 7 students (5 real, 2 phantom)
```

To override the file path:

```bash
python -m server admin assign --phantom-state /other/path.json
```

To disable phantom detection entirely:

```bash
python -m server admin assign --phantom-state ""
```

#### Step 5 - run the activity normally

Real students hunt as usual. Phantom meetings resolve instantly when the real
student enters the passphrase.

---

## Admin CLI reference

All commands require the server to be running.

```
python -m server admin <command> [options]
```

| Command | Description |
|---------|-------------|
| `students` | List all registered students |
| `assign` | Assign targets (reads `master_state.json` for phantoms automatically) |
| `assign --force` | Re-assign targets even if already assigned |
| `assign --phantom-state <path>` | Override phantom state file path |
| `announce <message>` | Broadcast a message to all clients |
| `time <N>` | Extend deadline by N minutes |
| `delete <uuid>` | Remove a student |
| `stats` | Show time remaining |

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

Request: `{ "forename": "Alice", "surname": "Smith" }`

Response: `{ "ok": true }`

---

#### `GET /targets/{uuid}`

Get the list of targets assigned to a student. Returns 404 until `assign` has
been run.

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

`passphrase_hint` is the first word of the target's passphrase.

---

#### `POST /meet`

Confirm a meeting. The finder submits the target's full passphrase.

Request:
```json
{ "finder_uuid": "<uuid>", "passphrase": "fox-in-the-henhouse" }
```

Success:
```json
{
  "ok": true,
  "target_uuid": "<uuid>",
  "target_forename": "Bob",
  "questions": ["What is your favourite travel memory?", "..."]
}
```

Failure:
```json
{ "ok": false, "reason": "that person is not one of your targets" }
```

---

#### `POST /answer`

Submit answers and record the meeting.

Request:
```json
{
  "finder_uuid": "<uuid>",
  "target_uuid": "<uuid>",
  "answers": [{ "question": "...", "answer": "..." }]
}
```

Response: `{ "ok": true, "meetings_completed": 2, "total_targets": 5 }`

Returns 409 if already recorded.

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

---

#### `GET /pending_meet/{uuid}`

Poll for a symmetric meeting notification. Called by the client's stats
poller to detect when another student has entered this student's passphrase.
Consumes the notification (one-shot).

Response: `{ "pending": false }` or
```json
{
  "pending": true,
  "finder_uuid": "<uuid>",
  "finder_forename": "Alice",
  "questions": ["..."]
}
```

---

#### `GET /time`

Get server clock and deadline.

Response:
```json
{
  "server_time": 1720000000.0,
  "deadline": 1720001200.0,
  "remaining_seconds": 1200.0
}
```

---

#### `GET /announcements?since=<timestamp>`

Poll for announcements sent after `since` (default 0).

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
| `POST` | `/admin/assign` | `{"force": false, "phantom_uuids": []}` | Assign targets |
| `POST` | `/admin/announce` | `{"message": "..."}` | Broadcast announcement |
| `POST` | `/admin/time` | `{"add_minutes": 5}` | Set deadline |

---

## Cross-platform release builds

### GitHub Actions (recommended)

Push a version tag to trigger a release build for all platforms:

```bash
git tag v1.0.0
git push origin v1.0.0
```

GitHub Actions builds five binaries in parallel and publishes them as a
GitHub Release:

| File | Platform |
|------|----------|
| `mag_client-linux-x86_64` | Linux, most desktops |
| `mag_client-linux-arm64` | Linux, Raspberry Pi, Snapdragon |
| `mag_client-windows-x86_64.exe` | Windows, most laptops |
| `mag_client-windows-arm64.exe` | Windows on Arm |
| `mag_client-macos-universal` | macOS, Intel and Apple Silicon |

### Setting the encryption key for releases

**Option A (recommended): set the XORKEY secret**

1. Generate a key: `bash scripts/genkey.sh`
2. Copy the output into **Settings -> Secrets -> Actions -> New repository
   secret** with the name `XORKEY`.
3. Put the same value in `app.toml -> encryption_key`.

The secret persists across all future releases.

**Option B: let the workflow generate one**

If `XORKEY` is not set, the workflow generates a random key per release and
uploads it as `XORKEY.txt` alongside the binaries. Download `XORKEY.txt` from
the release assets and copy the value into `app.toml` before running the
server.

### Local cross-compilation

`make release` cross-compiles all targets from a single Linux arm64 host
(e.g. Snapdragon X Elite / WSL2).

#### One-time toolchain setup

```bash
sudo scripts/setup-cross.sh
```

#### Build

```bash
make release XORKEY="$(grep encryption_key app.toml | cut -d'"' -f2)"

# Individual targets:
make release-linux-arm64
make release-linux-x86_64
make release-windows-x86_64
make release-windows-arm64
make release-macos          # macOS host only
```

Binaries land in `dist/`. Windows binaries are statically linked (no DLL
runtime required).

#### Toolchain files

| File | Target |
|------|--------|
| `cmake/toolchain-linux-x86_64.cmake` | Linux x86-64 via `x86_64-linux-gnu-g++` |
| `cmake/toolchain-windows-x86_64.cmake` | Windows x86-64 via MinGW-w64 posix |
| `cmake/toolchain-windows-arm64.cmake` | Windows arm64 via llvm-mingw clang++ |

The llvm-mingw root defaults to `/opt/llvm-mingw`. Override with:

```bash
make release-windows-arm64 LLVM_MINGW_ROOT=/usr/local/llvm-mingw
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

  scripts/
    genkey.sh           generate a random 32-char hex XOR key
    setup-cross.sh      install cross-compilers for local release builds

  src/
    server/             Python FastAPI server
      __main__.py       entry point (server or admin CLI)
      app/
        app.py          FastAPI application + lifespan
        config.py       AppConfig Pydantic model
        routes.py       student-facing endpoints
        admin.py        admin-only endpoints + check_all_done()
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
        targets.py      k-regular graph assignment + phantom grafting
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
    client/             Catch2 C++ tests
    system/             full system tests (real subprocesses)
      harness.py        ServerProcess + SimStudent helpers
      test_binary.py    headless binary tests
      test_scale.py     120-student scale test

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

### Database

The server uses SQLite with WAL journal mode. A single `DataStore` instance is
shared across all FastAPI threadpool workers. A `threading.Lock` serialises
every connection access.

Critical atomic operations:

- **Passphrase assignment**: `register_new_student()` reads used passphrases,
  picks an available one, and inserts the student record under one lock.
- **Meeting recording**: `add_meeting_if_not_exists()` checks for an existing
  meeting (in either direction) and inserts under one lock.

### Encryption

Student IDs are obfuscated with XOR before leaving the device. The same logic
runs in Python (server) and C++ (client):

```python
def encrypt_id(student_id: int, key_hex: str) -> str:
    key = bytes.fromhex(key_hex)
    plain = str(student_id).encode()
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(plain)).hex()
```

This is obfuscation, not encryption. It prevents casual interception on an
open LAN but is not resistant to an attacker with both the binary and a
network capture.

### Target graph algorithm

Targets are assigned as a k-regular graph via circular shift on a shuffled
ring. Edge cases:

- **n <= k**: complete graph (everyone meets everyone).
- **n * k is odd**: k is reduced by 1 (a regular graph requires an even degree sum).
- **Phantoms**: grafted onto the real graph after construction; each phantom is
  assigned min(k, n_real) random real students. No phantom-phantom edges are
  ever created.

### UDP discovery

The server runs two daemon threads:

- **Broadcaster**: sends `MAG_SERVER <ip> <port>` to `255.255.255.255` every 5 s.
- **Listener**: responds to `MAG_WHO` unicast.

The client sends `MAG_WHO` and listens for the response. On networks that
block broadcast, pass `--server ip:port` on the command line.

### Headless client mode

```
mag_client --headless 123456 --server 127.0.0.1:9876
```

Output is line-oriented `KEY=VALUE`. Exit codes: 0 success, 1 server not
found, 2 registration failed, 3 bad arguments.

### Logging

Two handlers configured by default in `src/server/logging/logger.toml`:

- **console**: INFO level, colourised via `rich`
- **file**: DEBUG level, rotating (10 MB per file, 3 backups)

---

## Troubleshooting

### Running the client

---

#### Windows: "Windows protected your PC" or nothing happens when double-clicking

Windows SmartScreen blocks executables downloaded from the internet that do
not have a code-signing certificate. The binary is safe; it just has not paid
for a certificate.

**How to run it anyway:**

1. Open PowerShell. You can do this in any of these ways:
   - Press **Win + R**, type `powershell`, press Enter.
   - Type `powershell` into the search bar in the Start menu and click
     **Windows PowerShell**.
   - Open File Explorer, navigate to the folder containing the binary,
     hold **Shift** and right-click an empty area, then choose
     **Open PowerShell window here**.

2. In the PowerShell window, type the name of the binary and press Enter:
   ```
   .\mag_client-windows-x86_64.exe
   ```
   (Use `mag_client-windows-arm64.exe` if your laptop has an Arm processor,
   such as a Snapdragon X or Microsoft SQ chip.)

3. If SmartScreen still appears, click **More info** then **Run anyway**.

Running via PowerShell instead of double-clicking bypasses most SmartScreen
prompts because you are explicitly invoking the binary yourself.

---

#### Linux / ChromeOS Linux: "Permission denied" or nothing happens

Downloaded files on Linux do not have the execute permission set by default.
Open a terminal in the folder containing the binary and run:

```bash
chmod +x ./mag_client-linux-x86_64-legacy   # or whichever filename you downloaded
./mag_client-linux-x86_64-legacy
```

`chmod +x` sets the execute bit, telling the OS this file is a program rather
than a data file.

---

#### Stuck on the discovery screen (throbber spinning, never connects)

**What is happening:**

When the client starts it tries to find the server automatically using a UDP
broadcast. A broadcast is a special network packet sent to every device on the
local network simultaneously, asking "is there a MAG server here?" The server
hears it and replies with its IP address.

This fails in two common situations:

**1. AP isolation (most school and home Wi-Fi routers)**

Many routers have a feature called AP isolation (also called client isolation
or station isolation). It prevents devices on the same Wi-Fi network from
talking directly to each other. This is a security feature — it stops one
student's laptop from attacking another — but it also blocks the broadcast
packets MAG uses for discovery.

**2. NAT between the client and the LAN (ChromeOS Linux, VMs, some VPNs)**

ChromeOS runs its Linux environment (the penguin / Crostini) inside a small
virtual machine. That VM sits behind a Network Address Translation (NAT) layer
managed by ChromeOS. NAT means the VM has its own private IP address
(typically `100.115.92.x`) and ChromeOS acts as a router between it and the
real network. Outgoing connections from the VM work fine, but incoming
broadcast packets sent to the Wi-Fi network never reach the VM because the NAT
layer does not know to forward them in.

**The fix — bypass discovery and connect directly:**

The instructor finds their machine's IP address:

```bash
# Linux / macOS
ip route get 1 | awk '{print $7; exit}'

# Windows (look for IPv4 Address under your Wi-Fi adapter)
ipconfig
```

Write the IP on the board. Students who are stuck on the discovery screen
close the client and relaunch it with the `--server` flag:

```
# Linux / ChromeOS Linux
./mag_client-linux-x86_64-legacy --server 192.168.1.42:9876

# Windows (in PowerShell)
.\mag_client-windows-x86_64.exe --server 192.168.1.42:9876

# macOS
./mag_client-macos-universal --server 192.168.1.42:9876
```

Replace `192.168.1.42` with the actual server IP. This skips the broadcast
entirely and connects directly, so AP isolation and NAT are no longer a
problem. Everything else (registration, the hunt, meetings) works identically.

---

### Server problems

---

#### "targets not yet assigned"

Run `python -m server admin assign` before students try to hunt.

---

#### Phantom UUIDs not detected

`master_state.json` must be in the directory where you run `admin assign`.
Pass `--phantom-state /path/to/file.json` to override.

---

#### Re-running a session from scratch

Delete `mag.db`, `mag.db-wal`, `mag.db-shm`, and `targets.json`, then restart
the server. All students must re-register. Delete `master_state.json` too if
you are changing the phantom set.

---

#### XORKEY mismatch

If clients register but their student ID lookup fails, the key baked into the
binary does not match `encryption_key` in `app.toml`. Rebuild the client (or
download the correct release) with the matching key.

---

## Security notes

- The admin token is sent as a Bearer token over plain HTTP. Use this system
  only on a trusted LAN (classroom Wi-Fi, direct ethernet, etc.).
- Student IDs are XOR-obfuscated, not encrypted. Do not rely on this for FERPA
  or similar privacy compliance.
- Rotate `encryption_key` and `admin_token` before each semester.
- The server has no rate limiting. If that is a concern, put nginx in front
  with a rate-limit rule.
