import pytest
from server.graph.targets import assign_targets


def _check(result, *, bidirectional=True, no_self=True):
    for u, neighbors in result.items():
        if no_self:
            assert u not in neighbors, f"{u} has self-loop"
        if bidirectional:
            for v in neighbors:
                assert u in result[v], f"edge {u}->{v} has no reverse"


def test_empty():
    assert assign_targets([], 5) == {}

def test_single():
    assert assign_targets(["a"], 5) == {"a": []}

def test_complete_graph_fallback_when_n_le_k():
    uuids = ["a", "b", "c"]
    result = assign_targets(uuids, 5)
    _check(result)
    for u in uuids:
        assert set(result[u]) == set(uuids) - {u}

def test_even_k_regular():
    uuids = [str(i) for i in range(10)]
    result = assign_targets(uuids, 4)
    _check(result)
    for neighbors in result.values():
        assert len(neighbors) == 4

def test_odd_k_regular_even_n():
    uuids = [str(i) for i in range(10)]
    result = assign_targets(uuids, 5)
    _check(result)
    for neighbors in result.values():
        assert len(neighbors) == 5

def test_odd_n_reduces_k():
    uuids = [str(i) for i in range(9)]
    result = assign_targets(uuids, 5)
    _check(result)
    for neighbors in result.values():
        assert len(neighbors) == 4   # reduced to keep n*k even

def test_no_duplicate_targets():
    uuids = [str(i) for i in range(12)]
    result = assign_targets(uuids, 5)
    for u, neighbors in result.items():
        assert len(neighbors) == len(set(neighbors))

def test_large_class():
    uuids = [str(i) for i in range(40)]
    result = assign_targets(uuids, 5)
    _check(result)

def test_minimum_n_for_k5():
    uuids = [str(i) for i in range(6)]
    result = assign_targets(uuids, 5)
    _check(result)
    for neighbors in result.values():
        assert len(neighbors) == 5

def test_deterministic_with_fixed_shuffle(monkeypatch):
    import random
    monkeypatch.setattr(random, "shuffle", lambda lst: lst.sort())
    uuids = list("abcdefgh")
    r1 = assign_targets(uuids[:], 3)
    r2 = assign_targets(uuids[:], 3)
    assert r1 == r2
