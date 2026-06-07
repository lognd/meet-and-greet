"""Target assignment: construct a k-regular graph over student UUIDs.

Bidirectional: if A has B as a target, B has A as a target.

Algorithm:
  - If n <= k, fall back to a complete graph (everyone meets everyone).
  - Otherwise use circular/shifted assignment: sort (shuffled) students into a
    ring and give each student their nearest k//2 neighbors on each side.
    For odd k the last neighbor is the diametrically opposite student (n//2
    away). This is O(n) and guarantees exactly k neighbors per student.
  - If n*k is odd (impossible for k-regular undirected graph), reduce k by 1.
"""

import random
from collections import defaultdict


def assign_targets(uuids: list[str], k: int) -> dict[str, list[str]]:
    n = len(uuids)
    if n < 2:
        return {uuids[0]: []} if n == 1 else {}

    # fall back to complete graph when not enough students
    if n <= k:
        return {u: [v for v in uuids if v != u] for u in uuids}

    # n*k must be even for a k-regular undirected graph
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

    # for odd k, also connect to the node n//2 steps away
    if effective_k % 2 == 1:
        for i, u in enumerate(order):
            v = order[(i + n // 2) % n]
            if v != u:
                adj[u].add(v)
                adj[v].add(u)

    return {u: list(neighbors) for u, neighbors in adj.items()}
