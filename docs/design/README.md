# Meet and Greet (MAG) - Design Document

## Overview

MAG is a classroom icebreaker application. A course-staff-run server assigns
students random "targets" to find. Students authenticate each other with
secret-agent passphrases, answer get-to-know-you questions, and earn credit
for each completed meeting.

---

## Architecture

```
     LAN (e.g. 192.168.1.0/24)
     
  [Server - staff laptop]          [Client - student laptop A]
  Python / FastAPI                 C++ binary (no dev tools needed)
  UDP broadcast (discovery)  <-->  UDP broadcast (discovery)
  HTTP :9876  <----------------->  HTTP client
  SQLite (persistent)
  Admin CLI (typer)

                                   [Client - student laptop B]
                                   ...

  [Master Client - staff laptop]
  Same binary, launched with --master flag
  Can register phantom students for those without a device
```

**Ports:**
| Protocol | Port | Purpose |
|----------|------|---------|
| UDP | 9875 | LAN discovery (broadcast) |
| TCP | 9876 | HTTP API |

Both ports are configurable in `app.toml`.

---

## Discovery Protocol

Problem: clients do not know the server's IP on the LAN.

**Server-side (every 5 seconds, periodic broadcast):**
```
UDP broadcast to 255.255.255.255:9875
Payload: MAG_SERVER <server_ip> <http_port>
Example: MAG_SERVER 192.168.1.42 9876
```

**Client joining the LAN (one-shot broadcast):**
```
UDP broadcast to 255.255.255.255:9875
Payload: MAG_WHO
```

**Server response to MAG_WHO (unicast back to sender):**
```
MAG_SERVER <server_ip> <http_port>
```

**Other clients receiving MAG_WHO:**  
Ignore silently. Clients only listen long enough to receive one `MAG_SERVER`
message, then stop listening on the UDP socket.

**Client behavior before server discovered:**  
Retries `MAG_WHO` broadcast every 3 seconds, displays "Searching for server..."

---

## Encryption

Student IDs must not travel in plaintext. A shared symmetric key (16 bytes,
hex string) is set in `app.toml` and compiled into the client binary via a
CMake define (`-DMAG_XORKEY="...">`).

**Algorithm:**
1. Convert student ID (uint64) to its decimal string representation.
2. XOR each byte of the string with bytes of the key (repeated/cycling).
3. Hex-encode the result.

Example (key `deadbeefcafebabedeadbeefcafebabe`):
```
id: 12345678  ->  string "12345678"
    XOR key bytes (cycling) -> raw bytes
    hex-encode -> "ab34ef..."  (sent in JSON as "encrypted_id")
```

This is intentionally lightweight - it prevents casual snooping on the wire,
not adversarial attacks.

---

## Data Models

### Student
```
uuid         TEXT PRIMARY KEY  (server-generated UUID4)
student_id   TEXT              (encrypted storage on server too)
forename     TEXT
surname      TEXT
passphrase   TEXT              (assigned at registration)
registered_at REAL             (unix timestamp)
```

### Meeting (a completed student-to-student interaction)
```
uuid         TEXT PRIMARY KEY
finder_uuid  TEXT REFERENCES students
target_uuid  TEXT REFERENCES students
met_at       REAL
answers      TEXT              (JSON array of {question, answer})
```

### Announcement
```
uuid         TEXT PRIMARY KEY
message      TEXT
sent_at      REAL
```

Assignments (targets) are stored in memory and also persisted as a JSON blob
(`targets.json`) next to the database so they survive server restarts.

---

## API Specification

Base URL: `http://<server_ip>:9876`

All request/response bodies are JSON. Admin endpoints require:
```
Authorization: Bearer <admin_token>
```

### Student Endpoints

#### POST /register
Register a new student or re-register after disconnect.

Request:
```json
{
  "encrypted_id": "ab12cd34...",
  "forename": "Jane",
  "surname": "Smith"
}
```

Response (new student):
```json
{
  "uuid": "xxxxxxxx-...",
  "passphrase": "eagle-has-landed",
  "is_new": true
}
```

Response (reconnect - student_id already exists):
```json
{
  "uuid": "xxxxxxxx-...",
  "passphrase": "eagle-has-landed",
  "is_new": false,
  "forename": "Jane",
  "surname": "Smith",
  "name_differs": false
}
```

If `name_differs` is true, client prompts whether to update. Update via
`PUT /student/<uuid>`.

#### PUT /student/<uuid>
Update forename/surname (idempotent, student can only update themselves).

Request:
```json
{ "forename": "Jane", "surname": "Smith" }
```

Response: `{ "ok": true }`

#### GET /targets/<uuid>
Get assigned targets. Returns 404 if targets not yet assigned.

Response:
```json
{
  "targets": [
    { "forename": "Bob", "surname": "Jones", "passphrase_hint": "eagle" },
    ...
  ],
  "assigned": true
}
```

Note: passphrase hint is only the first word of the target's passphrase
(before the first `-`). Full passphrase is verified server-side.

#### POST /meet
Authenticate a meeting using a target's passphrase.

Request:
```json
{
  "finder_uuid": "xxxxxxxx-...",
  "passphrase": "eagle-has-landed"
}
```

Response (success):
```json
{
  "ok": true,
  "target_uuid": "yyyyyyyy-...",
  "target_forename": "Bob",
  "questions": [
    "Where are you from?",
    "What is your superpower?"
  ]
}
```

Response (fail):
```json
{ "ok": false, "reason": "passphrase not recognized" }
```

Server validates: passphrase belongs to one of finder's targets AND the meeting
has not already been recorded.

#### POST /answer
Submit answers for a completed meeting.

Request:
```json
{
  "finder_uuid": "xxxxxxxx-...",
  "target_uuid": "yyyyyyyy-...",
  "answers": [
    { "question": "Where are you from?", "answer": "Chicago" },
    { "question": "What is your superpower?", "answer": "Making coffee" }
  ]
}
```

Response: `{ "ok": true, "meetings_completed": 3, "total_targets": 5 }`

#### GET /stats/<uuid>
Get a student's completion stats.

Response:
```json
{
  "meetings_completed": 5,
  "total_targets": 5,
  "finish_place": 2,
  "finish_ordinal": "2nd",
  "finished_at": 1717791600.0
}
```

#### GET /time
Get server time info (for clock sync).

Response:
```json
{
  "server_time": 1717791600.0,
  "deadline": 1717792200.0,
  "remaining_seconds": 600
}
```

#### GET /announcements
Poll for new announcements since a given timestamp.

Query param: `since=<unix_timestamp>` (default 0)

Response:
```json
{
  "announcements": [
    { "uuid": "...", "message": "Please wrap up!", "sent_at": 1717791700.0 }
  ]
}
```

---

### Admin Endpoints

All require `Authorization: Bearer <admin_token>`.

#### GET /admin/students
List all registered students.

#### DELETE /admin/student/<uuid>
Remove a student and their meetings.

#### POST /admin/assign
Trigger target assignment immediately (normally happens at deadline or when
staff presses the button).

Request: `{}` or `{ "force": true }` to re-assign.

#### POST /admin/announce
Broadcast a message to all clients.

Request: `{ "message": "Everyone please gather!" }`

#### POST /admin/time
Update the deadline.

Request: `{ "deadline": 1717792200.0 }` or `{ "add_minutes": 5 }`

---

## Target Assignment Algorithm

Given `n` students and `k=5` targets each (bidirectional):

1. If `n <= k + 1` (not enough students for k distinct targets), assign everyone
   to everyone (complete graph).
2. Otherwise, construct a random k-regular simple graph:
   - Shuffle student list.
   - Use "round robin" circular assignment: student `i` is connected to students
     `i+1, i+2, ..., i+k/2` and `i-1, i-2, ..., i-k/2` (mod n). For odd k,
     also add `i + n/2` (mod n).
   - This guarantees exactly k neighbors for each student in O(n) time.
3. If `n*k` is odd (impossible for k-regular graph), reduce k by 1 or fallback
   to complete graph.

The assignment is stored as a dict `{uuid -> [uuid, ...]}` in `targets.json`.

---

## Passphrase Bank

100+ phrases in the style of spy/military radio codes:
- Two or three hyphen-separated words
- Evocative and memorable
- Examples: `eagle-has-landed`, `fox-in-the-henhouse`, `operation-nightfall`

Passphrases are drawn without replacement per session. If student count exceeds
bank size, passphrases cycle with a suffix number.

---

## Question Bank

25 questions, split:
- **First 12 (standard):** name, major, year, hometown, hobbies, etc.
- **Last 13 (interesting):** unusual life experiences, opinions, hypotheticals

Each student-student meeting gets 3 randomly selected questions (at least one
from each half).

---

## Configuration (`app.toml`)

```toml
# Network
port = 9876           # HTTP port
udp_port = 9875       # UDP discovery port

# Security
# 32-char hex string (16 bytes). Must match client compile-time MAG_XORKEY.
encryption_key = "deadbeefcafebabedeadbeefcafebabe"
admin_token = "change-me-before-use"

# Session
time_limit_minutes = 20     # 0 = no limit
targets_per_student = 5     # k in k-regular graph

# Storage
db_file = "mag.db"
targets_file = "targets.json"
log_file = "mag.log"

# Behavior
questions_per_meeting = 3
```

---

## Admin CLI

Launched via: `python -m server admin`

Commands:
```
mag admin students          # list all students
mag admin delete <uuid>     # remove a student
mag admin assign            # trigger target assignment
mag admin announce <msg>    # broadcast message
mag admin stats             # show completion stats
mag admin time <minutes>    # add N minutes to deadline
```

The CLI hits the same HTTP API using the admin token from `app.toml`.

---

## Master Client

Launch client with `./mag_client --master` to enter master mode.

In master mode the staff can:
- Register phantom students (enter name + ID manually).
- View all phantom students and their passphrases.
- Assign a phantom student's passphrase to a real student sitting nearby
  (for students without a device).
- Track which phantom students have been "found."

Master state is stored locally in `master_state.json`.

---

## Client State Machine

```
DISCOVERING   -> broadcast MAG_WHO, wait for MAG_SERVER
     |
REGISTERING   -> POST /register, show passphrase to user
     |
WAITING       -> poll GET /targets/<uuid> every 5s, show "Waiting..."
     |            poll GET /time for countdown
HUNTING       -> show target list (rotate with arrow keys / Enter)
     |            prompt passphrase entry
MEETING       -> POST /meet -> receive questions -> prompt answers
     |            POST /answer
STATS         -> show completion stats, finish place
```

Announcement polling runs in a background thread throughout WAITING/HUNTING/
MEETING states. Announcements interrupt the current display with a full-screen
message, then return.

---

## Build & Deploy

**Server:**
```sh
pip install -e .
python -m server              # start server (reads app.toml)
python -m server admin        # admin CLI
```

**Client (Linux/macOS):**
```sh
cmake -B build -DMAG_XORKEY="deadbeefcafebabedeadbeefcafebabe"
cmake --build build
./build/mag_client
```

**Client (Windows - cross-compile via WSL or MSVC):**
Same CMake invocation; cpp-httplib and the UDP code use Winsock2 on Windows.

---

## Security Notes

- XOR encryption is obfuscation, not security. It prevents casual packet
  inspection, not determined attackers on the LAN.
- Admin token should be changed before each session (`app.toml`).
- No HTTPS - this runs on a trusted classroom LAN only.
- Student UUIDs are server-generated; clients cannot forge another student's
  identity.

---

## File Layout

```
meet-and-greet/
  app.toml                      server config (gitignored in prod)
  app.toml.example              template with docs
  CMakeLists.txt                C++ client build
  pyproject.toml                Python server packaging
  docs/design/README.md         this document
  include/client/
    data.h                      C++ data structs + XOR cipher
    network.h                   C++ network declarations
  src/
    client/
      main.cpp                  client entry point + TUI
      network/
        http.cpp                HTTP client impl
        udp.cpp                 UDP discovery impl
    server/
      __main__.py               server entry point
      app/
        __init__.py
        app.py                  App orchestrator
        config.py               AppConfig (Pydantic)
        routes.py               FastAPI student routes
        admin.py                FastAPI admin routes
      cli/
        __init__.py
        admin.py                Typer admin CLI
      crypto/
        __init__.py
        cipher.py               XOR encrypt/decrypt
      data/
        __init__.py
        passphrases.py          passphrase bank (100+)
        questions.py            question bank (25)
      db/
        __init__.py
        models.py               dataclasses for DB rows
        store.py                DataStore (sqlite3)
      discovery/
        __init__.py
        udp.py                  UDP broadcast server
      graph/
        __init__.py
        targets.py              k-regular graph assignment
      logging/
        __init__.py
        logger.py               get_logger()
        logger.toml             dictConfig
      network/
        __init__.py
        udp.py                  (same as discovery, re-exported)
```
