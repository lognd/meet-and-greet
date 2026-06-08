"""Target assignment: construct a k-regular graph over student UUIDs.

Bidirectional: if A has B as a target, B has A as a target.

Phantom students are virtual (no real person behind them).  They must never
be assigned each other as targets because neither can initiate a meeting.
Pass their UUIDs in ``phantom_uuids`` and the algorithm guarantees that every
edge in the resulting graph has at least one real endpoint.

Algorithm (real students only, then phantoms grafted on):
  - Sort shuffled real students into a ring; each gets their k nearest
    neighbours.  For n <= k fall back to a complete graph.
  - Each phantom is then assigned min(k, len(real)) real neighbours,
    chosen uniformly at random.  Those real students get the phantom added
    to their target list (may exceed k, which is acceptable).
  - If n*k is odd the effective k is reduced by 1 to keep the graph regular.
"""

import random
from collections import defaultdict
from typing import Collection


def _assign_regular(uuids: list[str], k: int) -> dict[str, list[str]]:
    """k-regular undirected graph on uuids (no phantom awareness)."""
    n = len(uuids)
    if n < 2:
        return {uuids[0]: []} if n == 1 else {}
    if n <= k:
        return {u: [v for v in uuids if v != u] for u in uuids}

    effective_k = k if (n * k) % 2 == 0 else k - 1
    if effective_k < 1:
        return {u: [v for v in uuids if v != u] for u in uuids}

    order = uuids[:]
    random.shuffle(order)

    adj: dict[str, set[str]] = defaultdict(set)
    half = effective_k // 2
    for i, u in enumerate(order):
        for d in range(1, half + 1):
            v = order[(i + d) % n]
            adj[u].add(v)
            adj[v].add(u)

    if effective_k % 2 == 1:
        for i, u in enumerate(order):
            v = order[(i + n // 2) % n]
            if v != u:
                adj[u].add(v)
                adj[v].add(u)

    return {u: list(neighbors) for u, neighbors in adj.items()}


def assign_targets(
    uuids: list[str],
    k: int,
    phantom_uuids: Collection[str] = (),
) -> dict[str, list[str]]:
    """Build target assignments, keeping phantom students away from each other.

    Guarantees:
      - Every real student has exactly k targets (or n-1 if n <= k), all of
        which may be real or phantom.
      - Every phantom student has min(k, len(real)) real targets.
      - No phantom-phantom edge exists.
    """
    ph_set = frozenset(phantom_uuids)
    real    = [u for u in uuids if u not in ph_set]
    phantoms = [u for u in uuids if u in ph_set]

    if not real:
        return {}

    # Build the regular graph among real students only.
    adj: dict[str, list[str]] = _assign_regular(real, k)

    # Graft each phantom onto k randomly chosen real students.
    pool = list(real)
    for ph in phantoms:
        random.shuffle(pool)
        chosen = pool[:min(k, len(pool))]
        adj[ph] = chosen[:]
        for r in chosen:
            if ph not in adj.get(r, []):
                adj.setdefault(r, []).append(ph)

    return adj
