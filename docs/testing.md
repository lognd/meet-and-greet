# Testing Guide

## Quick-start: single machine (all real students)

```
# Terminal 1 - server
cd src/server
python -m server

# Terminal 2 - admin (after students have joined)
python -m server admin students          # verify everyone registered
python -m server admin assign            # assign targets
python -m server admin announce "Go!"    # optional kick-off message

# Terminals 3+ - one per student
./build/mag_client --host 127.0.0.1 --name "Alice Smith"
./build/mag_client --host 127.0.0.1 --name "Bob Jones"
```

---

## Mixed phantom testing

Phantoms are virtual students with no real person behind them.  They are
registered through master mode so the server believes they exist, but no
human will ever type their passphrase.  The symmetric auto-meet logic handles
this correctly: when a real student enters a phantom's passphrase the meeting
is recorded on both sides automatically, so neither student is permanently
blocked.

### Step 1 - start the server

```
cd src/server
python -m server
```

### Step 2 - register real students

Open one terminal per real participant and run:

```
./build/mag_client --host <server-ip>
```

Each client will prompt for a name, discover the server, and register.
Leave these running.

### Step 3 - register phantom students

On any machine (typically the server machine), run master mode once for each
phantom you want to add:

```
./build/mag_client --host <server-ip> --master --name "Phantom One"
./build/mag_client --host <server-ip> --master --name "Phantom Two"
```

Master mode registers the student, prints their UUID and passphrase, then
writes `master_state.json` in the working directory.  Each run appends to the
file.  Example output:

```
Registered: Phantom One (uuid=abc123..., passphrase=maple-river-seven)
State written to master_state.json
```

### Step 4 - assign targets

```
python -m server admin assign
```

The CLI reads `master_state.json` automatically and passes the phantom UUIDs
to the server.  You will see:

```
Detected 2 phantom(s) from master_state.json
Assigned targets for 7 students (5 real, 2 phantom)
```

The assignment guarantees:
- No phantom is assigned another phantom as a target.
- Each phantom is assigned up to k real students as targets.
- Each of those real students has the phantom in their target list.

### Step 5 - run the activity

Real students hunt normally.  When a real student enters a phantom's
passphrase the server records the meeting immediately and also places a
pending-meet notification for the phantom UUID.  The phantom has no client
polling that endpoint, so the notification is simply discarded on the next
GC cycle.  The real student's progress counter advances correctly.

### Step 6 - verify completion and auto-exit

When every real student has met all of their targets the server logs:

```
All students finished - shutting down in 30 s
```

After 30 seconds it exits cleanly via SIGTERM, giving clients time to display
their final stats screen.  Phantoms are excluded from the completion check.

---

## Useful admin commands

| Command | Effect |
|---------|--------|
| `python -m server admin students` | List all registered students with UUIDs and passphrases |
| `python -m server admin assign` | Assign targets (reads master_state.json automatically) |
| `python -m server admin assign --force` | Re-assign even if targets already exist |
| `python -m server admin announce "msg"` | Broadcast a message to all connected clients |
| `python -m server admin time 5` | Add 5 minutes to the session deadline |
| `python -m server admin stats` | Show time remaining |
| `python -m server admin delete <uuid>` | Remove a student before assignment |

---

## Troubleshooting

**Client stuck on "Connecting..."**
The server may not be reachable.  Confirm the server IP and that UDP port
9876 is not blocked by a firewall.  On Linux: `ss -ulnp | grep 9876`.

**"targets not yet assigned"**
Run `python -m server admin assign` before students try to hunt.

**Phantom UUIDs not detected**
`master_state.json` must be in the directory where you run `python -m server
admin assign`.  Pass an explicit path with `--phantom-state /path/to/file.json`
if needed.  Pass `--phantom-state ""` to disable phantom detection entirely.

**Windows client crashes on target assignment**
Ensure you are running a build from after commit that fixed the `sel`
heap-allocation bug in `nav_hunt_menu`.

**Re-running a session from scratch**
Delete `db.sqlite` and `targets.json`, then restart the server.  All
students must re-register.  Also delete `master_state.json` if you are
changing the phantom set.
