# Cleaner callback lifetime regressions

This maintained regression complements the [real-daemon shutdown demonstration](../cleaning-recovery/README.md) for existing issue #334 / PR #335. It adds no production change and does not create a new bounty claim.

The fixture compiles the complete `kvlds/btree_cleaning.c` directly, as the existing allocation-failure regression already did. Btree types and headers are the repository's real definitions. Node operations, timers, and event delivery retain deterministic fixture collaborators rather than running a page pool or daemon. The fixture constructs the pending-callback state explicitly; the separate real-daemon demonstration establishes the corresponding ordinary-update execution.

## Cases

1. Two pending SHADOW-leaf callbacks: the first must not free a group with a sibling callback pending; the final abandonment must release the empty group so the real `btree_cleaning_stop()` can finish. No allocation is forced to fail in this case.
2. One CLEAN leaf and one SHADOW leaf: the final abandoned fetch must not free a group which still owns a clean node. `btree_cleaning_notify_dirtying()` subsequently releases that node and group through the normal notification path.
3. Two groups: completing the older, non-head group must preserve the newer group's forward/backward links, pending count, and later shutdown.
4. The original allocation-failure regression: `callback_clean()` must return its pending-clean count, unlock once, and release an otherwise empty group. The original assertions are preserved.

## Recorded paired result — October 4, 2026

Environment: Linux x86_64, kernel 6.18.44, glibc 2.41; GCC 14.2.0 (Debian 14.2.0-19). Both compile invocations returned 0 with `-Wall -Wextra -Werror` and no diagnostics. One bounded execution of each variant was performed; no repository-wide suite was run.

| Production input | Git blob | Result |
| --- | --- | --- |
| Upstream cleaner at `3de151b5d878b0714552c3658562eb5a87b378ce` | `f99dff5ee5e8f49694684921675d11b580c718b6` | SIGABRT (Python return code -6) in the real `btree_cleaning_stop()` assertion. |
| Existing PR335 cleaner at `1ebc81a184c653992995a32422c92669e123bb6d` | `db5554527e09bf8f994ae51f85c81ef4bc87bb4d` | Exit 0; all four cases passed; stderr empty. |

The same test source and headers were used for both variants. The upstream file was reconstructed by reversing the existing PR's cleaner diff and checked against its exact Git blob, rather than treating a hand-rewritten approximation as the baseline. The later real-daemon evidence commit `c983cf6a2a33c2b11bb3e1d8f8d27571fbd3e6a6` changes neither production input nor this regression's preimage and is preserved in publication.

Baseline stdout before the production assertion:

```text
shadow shutdown: pending=0, group_pending=0, group_left=1
```

Baseline diagnostic, with the temporary checkout prefix omitted:

```text
btree_cleaning.c:595: btree_cleaning_stop: Assertion `C->head == NULL' failed.
```

Existing-patch stdout:

```text
shadow shutdown: pending=0, group_pending=0, group_left=0
PASS: shadow callbacks retain pending sibling and allow shutdown
PASS: clean sibling remains owned until normal dirty notification
PASS: non-head group completion preserves sibling links
PASS: callback_clean failure releases pending clean and group
```

The baseline stops at the first case; it did not execute the remaining controls. The repaired variant executes all four. This result must not be represented as an additional daemon run, filesystem durability measurement, default-workload failure frequency, or a demonstrated data-loss condition. Sponsor acceptance and bounty classification remain separate.

## Focused reproduction

From a checkout containing this test and the desired cleaner variant, with assertions enabled:

```sh
cc -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
  -O0 -g -Wall -Wextra -Werror \
  -Ikvlds -Ilibcperciva/events -Ilibcperciva/util \
  tests/btree-cleaning-failure/main.c -o /tmp/cleaner-lifetime
/tmp/cleaner-lifetime
```

Use a disposable checkout for the baseline comparison and the exact production blob identified above. The test's existing Makefile integration remains unchanged. Running with `NDEBUG` is unsupported because, like the original fixture, this program uses assertions to exercise and check operations.

## Exact executed bytes

| Input | SHA-256 |
| --- | --- |
| Expanded `main.c` | `901d6aad3e91881559c767708a464fca890af6066c3bc2bfe1cfdbd794fcaa05` |
| Baseline cleaner | `82540e314bf251e669fb8c00ffaf8a331dc57b51f91d34f21ecdb934ab1c7b95` |
| Existing-patch cleaner | `dad97fc805281f9a844b5a73080cccd6fd6d516640e6726f266a68c8763c1ffe` |
