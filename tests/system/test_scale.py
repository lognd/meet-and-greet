"""
Full-scale system test: 120 students, complete registration -> hunt -> finish.

Architecture
------------
1. ServerProcess starts on isolated ports in a temp directory.
2. 120 SimStudent instances register concurrently (ThreadPoolExecutor).
3. Admin assigns targets (k=5, bidirectional graph).
4. All 120 students hunt concurrently: each meets their assigned targets using
   the passphrase_map built from step 2.
5. Assertions verify every student completed all their meetings.

Concurrency notes
-----------------
- SQLite WAL + busy_timeout=10000 handles concurrent writes.
- "already met" responses are expected and treated as success (bidirectional).
- A shared threading.Event synchronises registration -> assign -> hunt phases.
- Total wall-clock time is typically 5-15 s on a modern machine.
"""

import concurrent.futures
import threading
import time
import pytest

from tests.system.harness import ServerProcess, SimStudent

# ---------------------------------------------------------------------------
# Ports
# ---------------------------------------------------------------------------

_PORT     = 19890
_UDP_PORT = 19891

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _register_batch(
    server: ServerProcess,
    student_ids: list[int],
    *,
    workers: int = 30,
) -> tuple[list[SimStudent], dict[str, str]]:
    """Register all students concurrently.

    Returns (students, passphrase_map) where passphrase_map is {uuid: passphrase}.
    """
    students: list[SimStudent | None] = [None] * len(student_ids)
    lock = threading.Lock()

    def _reg(idx: int) -> None:
        sid = student_ids[idx]
        s = SimStudent(
            student_id=sid,
            forename=f"Student{sid:05d}",
            surname="Test",
            server=server,
        )
        s.register()
        with lock:
            students[idx] = s

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        futures = {ex.submit(_reg, i): i for i in range(len(student_ids))}
        for fut in concurrent.futures.as_completed(futures):
            fut.result()  # re-raise any exception

    assert all(s is not None for s in students)
    passphrase_map = {s.uuid: s.passphrase for s in students}
    return students, passphrase_map


def _hunt_batch(
    students: list[SimStudent],
    passphrase_map: dict[str, str],
    *,
    workers: int = 40,
    poll_timeout: float = 60.0,
) -> list[dict]:
    """All students hunt concurrently. Returns list of final stats dicts."""
    results: list[dict | None] = [None] * len(students)
    lock = threading.Lock()
    errors: list[str] = []

    def _hunt(idx: int) -> None:
        s = students[idx]
        try:
            targets = s.get_targets(poll_timeout=poll_timeout)
            for target in targets:
                t_uuid = target["uuid"]
                t_pass = passphrase_map.get(t_uuid)
                if t_pass is None:
                    raise AssertionError(f"No passphrase for target {t_uuid}")

                meet_r = s.meet(t_pass)
                if meet_r["ok"]:
                    try:
                        s.submit_answers(meet_r["target_uuid"], meet_r.get("questions", []))
                    except RuntimeError as exc:
                        if "409" not in str(exc):
                            raise
                        # bidirectional: the other side already submitted - ok
                elif "already met" in meet_r.get("reason", ""):
                    pass  # recorded from the other side - bidirectional
                else:
                    raise AssertionError(
                        f"{s.uuid} failed to meet {t_uuid}: {meet_r.get('reason')}"
                    )

            stats = s.get_stats()
            with lock:
                results[idx] = stats
        except Exception as e:
            with lock:
                errors.append(f"Student {s.uuid}: {e}")

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        futures = [ex.submit(_hunt, i) for i in range(len(students))]
        concurrent.futures.wait(futures)

    if errors:
        raise AssertionError("Hunt failures:\n" + "\n".join(errors))
    return results


# ---------------------------------------------------------------------------
# Scale test
# ---------------------------------------------------------------------------

class TestScaleOneHundredTwentyStudents:
    """End-to-end test with 120 concurrent simulated students."""

    def test_registration_all_succeed(self):
        student_ids = list(range(1_000_000, 1_000_120))
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=5) as srv:
            students, _ = _register_batch(srv, student_ids)
        assert len(students) == 120
        assert len({s.uuid for s in students}) == 120, "UUIDs not unique"

    def test_all_passphrases_unique(self):
        student_ids = list(range(1_100_000, 1_100_120))
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=5) as srv:
            students, _ = _register_batch(srv, student_ids)
        passphrases = [s.passphrase for s in students]
        assert len(passphrases) == len(set(passphrases)), "Duplicate passphrases found"

    def test_target_assignment_k5_regular(self):
        student_ids = list(range(1_200_000, 1_200_120))
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=5) as srv:
            students, _ = _register_batch(srv, student_ids)
            srv.assign_targets()

            # Every student has exactly 5 targets (120 students, k=5, n*k=600 even -> valid)
            for s in students:
                targets = srv.get(f"/targets/{s.uuid}")["targets"]
                assert len(targets) == 5, (
                    f"{s.uuid} has {len(targets)} targets, expected 5"
                )

    def test_target_assignment_is_bidirectional(self):
        student_ids = list(range(1_300_000, 1_300_120))
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=5) as srv:
            students, _ = _register_batch(srv, student_ids)
            srv.assign_targets()

            uuid_to_targets: dict[str, set[str]] = {}
            for s in students:
                targets = srv.get(f"/targets/{s.uuid}")["targets"]
                uuid_to_targets[s.uuid] = {t["uuid"] for t in targets}

            # For each edge A->B, B->A must exist
            violations = []
            for u, neighbors in uuid_to_targets.items():
                for v in neighbors:
                    if u not in uuid_to_targets.get(v, set()):
                        violations.append(f"{u} -> {v} but not reverse")
            assert not violations, "\n".join(violations[:5])

    def test_full_flow_all_meetings_complete(self):
        """120 students register, get targets, meet everyone, verify completion."""
        student_ids = list(range(1_400_000, 1_400_120))

        with ServerProcess(
            _PORT, _UDP_PORT,
            targets_per_student=5,
            questions_per_meeting=2,
        ) as srv:
            t0 = time.time()

            # Phase 1: register all
            students, passphrase_map = _register_batch(srv, student_ids, workers=30)
            t_reg = time.time() - t0

            # Phase 2: assign targets
            assign_r = srv.assign_targets()
            assert assign_r["ok"] is True
            assert assign_r["students"] == 120

            # Phase 3: all students hunt concurrently
            final_stats = _hunt_batch(
                students, passphrase_map, workers=40, poll_timeout=60.0
            )
            t_total = time.time() - t0

            print(f"\n  Registration:  {t_reg:.1f}s")
            print(f"  Total:         {t_total:.1f}s")

            # Phase 4: verify
            failures = []
            for s, stats in zip(students, final_stats):
                if stats is None:
                    failures.append(f"{s.uuid}: no stats returned")
                    continue
                completed = stats["meetings_completed"]
                total     = stats["total_targets"]
                if completed != total:
                    failures.append(
                        f"{s.uuid} ({s.forename}): {completed}/{total} meetings"
                    )

            assert not failures, (
                f"{len(failures)} students did not complete all meetings:\n"
                + "\n".join(failures[:10])
            )

    def test_finish_place_assigned_to_first_finisher(self):
        """The first student to complete all meetings should get finish_place=1."""
        student_ids = list(range(1_500_000, 1_500_120))

        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=5) as srv:
            students, passphrase_map = _register_batch(srv, student_ids, workers=30)
            srv.assign_targets()

            # Run full hunt
            _hunt_batch(students, passphrase_map, workers=40)

            # At least one student should have finish_place == 1
            places = []
            for s in students:
                stats = s.get_stats()
                if stats.get("finish_place"):
                    places.append(stats["finish_place"])

            assert 1 in places, "No student has finish_place=1"

    def test_admin_can_delete_student_mid_session(self):
        """Deleting a student does not crash the server or other students."""
        student_ids = list(range(1_600_000, 1_600_010))  # 10 students

        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=2) as srv:
            students, passphrase_map = _register_batch(srv, student_ids, workers=10)
            srv.assign_targets()

            # Delete the first student
            victim = students[0]
            srv.admin_delete(f"/admin/student/{victim.uuid}")

            # Remaining 9 students should still be listed
            remaining = srv.all_students()
            assert len(remaining) == 9
            assert all(s["uuid"] != victim.uuid for s in remaining)

    def test_announcement_received_by_all_pollers(self):
        """Announcements posted mid-session are visible to all clients."""
        student_ids = list(range(1_700_000, 1_700_005))

        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=2) as srv:
            students, _ = _register_batch(srv, student_ids, workers=5)
            srv.announce("Test broadcast message")

            # All students can poll and see the announcement
            for s in students:
                anns = srv.get("/announcements")["announcements"]
                messages = [a["message"] for a in anns]
                assert "Test broadcast message" in messages

    def test_time_remaining_decrements(self):
        """Server time countdown works correctly."""
        with ServerProcess(
            _PORT, _UDP_PORT,
            targets_per_student=2,
            time_limit_minutes=1,
        ) as srv:
            t1 = srv.get("/time")
            time.sleep(1.1)
            t2 = srv.get("/time")

            r1 = t1["remaining_seconds"]
            r2 = t2["remaining_seconds"]
            assert r1 is not None and r2 is not None
            assert r2 < r1, f"Time did not decrease: {r1} -> {r2}"

    def test_reconnect_preserves_meetings(self):
        """A student who reconnects should still see their completed meetings."""
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=1) as srv:
            s1 = SimStudent(1_800_001, "Reconnect", "Alice", srv).register()
            s2 = SimStudent(1_800_002, "Reconnect", "Bob",   srv).register()
            srv.assign_targets()

            # Alice meets Bob
            targets = srv.get(f"/targets/{s1.uuid}")["targets"]
            assert len(targets) == 1
            meet_r = srv.post("/meet", {
                "finder_uuid": s1.uuid,
                "passphrase":  s2.passphrase,
            })
            if meet_r["ok"]:
                srv.post("/answer", {
                    "finder_uuid": s1.uuid,
                    "target_uuid": s2.uuid,
                    "answers": [{"question": "Q?", "answer": "A"}],
                })

            # Alice "reconnects" (re-registers with same ID)
            s1_again = SimStudent(1_800_001, "Reconnect", "Alice", srv).register()
            assert s1_again.uuid == s1.uuid
            assert s1_again.is_new is False

            # Stats should still show the meeting
            stats = s1_again.get_stats()
            assert stats["meetings_completed"] == 1


# ---------------------------------------------------------------------------
# Smaller sanity-check scale tests
# ---------------------------------------------------------------------------

class TestScaleSmall:
    """Faster scale tests covering edge cases with small class sizes."""

    def test_two_students_complete_flow(self):
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=1) as srv:
            s1 = SimStudent(2_000_001, "Two", "One", srv)
            s2 = SimStudent(2_000_002, "Two", "Two", srv)
            s1.register()
            s2.register()
            pmap = {s1.uuid: s1.passphrase, s2.uuid: s2.passphrase}
            srv.assign_targets()
            stats = _hunt_batch([s1, s2], pmap, workers=2)
            for st in stats:
                assert st["meetings_completed"] == st["total_targets"]

    def test_six_students_k5_complete_graph(self):
        """6 students with k=5 -> everyone meets everyone (complete graph)."""
        student_ids = list(range(2_100_001, 2_100_007))
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=5) as srv:
            students, pmap = _register_batch(srv, student_ids, workers=6)
            srv.assign_targets()
            # Each should have 5 targets (complete graph)
            for s in students:
                targets = srv.get(f"/targets/{s.uuid}")["targets"]
                assert len(targets) == 5

    def test_odd_student_count_k_reduced(self):
        """9 students with k=5 -> k reduced to 4 (9*5=45 is odd)."""
        student_ids = list(range(2_200_001, 2_200_010))
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=5) as srv:
            students, pmap = _register_batch(srv, student_ids, workers=9)
            srv.assign_targets()
            for s in students:
                targets = srv.get(f"/targets/{s.uuid}")["targets"]
                assert len(targets) == 4   # reduced from 5

    def test_force_reassign_works(self):
        student_ids = list(range(2_300_001, 2_300_011))
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=2) as srv:
            students, _ = _register_batch(srv, student_ids, workers=10)
            r1 = srv.assign_targets()
            r2 = srv.assign_targets()          # no force -> should fail
            r3 = srv.assign_targets(force=True)
            assert r1["ok"] is True
            assert r2["ok"] is False
            assert r3["ok"] is True

    def test_wrong_passphrase_does_not_record_meeting(self):
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=1) as srv:
            s1 = SimStudent(2_400_001, "Wrong", "Pass", srv).register()
            s2 = SimStudent(2_400_002, "Wrong", "Pass", srv).register()
            srv.assign_targets()

            meet_r = srv.post("/meet", {
                "finder_uuid": s1.uuid,
                "passphrase": "completely-wrong-passphrase",
            })
            assert meet_r["ok"] is False

            # Stats still zero
            stats = s1.get_stats()
            assert stats["meetings_completed"] == 0

    def test_duplicate_answer_rejected(self):
        with ServerProcess(_PORT, _UDP_PORT, targets_per_student=1) as srv:
            s1 = SimStudent(2_500_001, "Dup", "Answer", srv).register()
            s2 = SimStudent(2_500_002, "Dup", "Answer", srv).register()
            srv.assign_targets()

            meet_r = srv.post("/meet", {
                "finder_uuid": s1.uuid, "passphrase": s2.passphrase
            })
            if meet_r["ok"]:
                payload = {
                    "finder_uuid": s1.uuid,
                    "target_uuid": s2.uuid,
                    "answers": [{"question": "Q?", "answer": "A"}],
                }
                r1 = srv.post("/answer", payload)
                assert r1["ok"] is True

                # Second submit should 409
                import urllib.error
                with pytest.raises((RuntimeError,)):
                    srv.post("/answer", payload)
