# Real-daemon cleaner shutdown crash (existing issue #334)

This supplements the existing first report and PR #335 with an actual daemon
crash during ordinary updates. It does not rely on malloc failure or a manually
constructed cleaner/tree state.

## Recorded result — October 4, 2026

The baseline is unmodified upstream
`787805e011a62c7b8a87bfae1ed156e5adb4af2c`. The counterfactual uses that same
source with only the already-proposed empty-group release at `callback_clean`'s
successful `done` label. The executable change is the same as this PR, with a
shorter explanatory comment. No other C source differs in the compared tree.
This does not claim execution of the complete older PR or its allocation-failure
regressions.

Both variants ran the real LBS and KVLDS daemons, repository protocol/CRC code,
background cleaner timers, filesystem, and storage operations. The same C client
performed 64 initial SETs, one update to create reclaimable pages, another batch
of 64 SETs, and 64 GETs which verified the exact final 128-byte values. A local
proxy delayed one genuine LBS APPEND response for 2.2 seconds without modifying
its bytes. The client then disconnected from a KVLDS process started with `-1`,
which requests normal shutdown after one client.

| Observation | Baseline | Existing empty-group release |
| --- | --- | --- |
| All 129 SETs acknowledged | Yes | Yes |
| All 64 GETs matched their exact final values | Yes | Yes |
| Client process exit | 0 | 0 |
| KVLDS process exit | SIGABRT (Python return code -6) | 0 |
| KVLDS stderr | `btree_cleaning_stop: Assertion C->head == NULL failed` | Empty |
| Client wall time, single observation | 2.203872 s | 2.204855 s |

The baseline's exact diagnostic was:

```text
kvlds-before: btree_cleaning.c:595: btree_cleaning_stop: Assertion `C->head == NULL' failed.
```

This is an actual unsanitized process abort after successful requests, not an
assertion in a test double. The counterfactual completed normal shutdown. No
stored-data loss is established, and no post-restart durability comparison was
performed. The wall times are not a benchmark or service-level guarantee.

## Trigger and source relationship

The deliberately aggressive, valid storage-cost setting `-S 1000000000000`
makes the background cleaner act during a short demonstration instead of waiting
for a production workload to accumulate its usual cleaning debt. `-C 1024` sets
the page-cache count; LBS uses 1,024-byte blocks and one reader worker. The delay
is a controlled transport timing condition, not a failed or fabricated storage
response. These settings do not describe default-rate frequency or probability.

While the update awaits its acknowledgement, old shadow leaves can be selected
by the normal cleaner. When `callback_clean` finds those leaves are no longer
CLEAN it returns their pending-clean count and releases their node locks. Without
the fix, the empty group remains in the cleaner list; no node acquired a cleaning
cookie whose later destruction would free it. Shutdown reaches its final
empty-list assertion and aborts.

The sole compared source change is:

```diff
 done:
+    /* Release an empty group after its last abandoned fetch. */
+    if ((CG->head == NULL) && (CG->pending_fetches == 0))
+        free_cg(CG);
+
     /* Success! */
     return (0);
```

It is at the `done` label in `callback_clean`, not the similarly named labels in
other functions. The failure-path accounting changes elsewhere in this PR were
not needed for this reproduction and were not included in the counterfactual.

## Environment and bounds

Linux x86_64, kernel 6.18.44, glibc 2.41, GCC 14.2.0
(Debian 14.2.0-19). The actual daemon binaries use the repository Makefiles and
normal assertions, without sanitizers or forced allocation errors. Only the
required daemon targets and focused protocol client were built; no full suite
or unrelated benchmark ran.

One completed baseline/counterfactual pair used the identical client and proxy.
Two earlier fixture attempts stopped during initialization because `-c` denotes
cache *bytes*, not pages; they did not execute the update path and are not counted
as bug reproductions. The final invocation uses `-C 1024` for both variants. Their
setup-error logs are retained separately in the execution bundle.

Client execution is bounded to 12 seconds and daemon shutdown observation to
three seconds. Each variant uses its own new temporary directory and Unix
sockets. Cleanup terminates/reaps only PIDs recorded for that variant. The
published proxy uses process-local Linux subreaper mode for those daemon children;
it does not alter a shared daemon or external service.

## Reproduce

`client.c` and `probe.py` are the exact final executed fixture bytes. The script
expects the following disposable directory layout:

```text
run/
  lbs-recovery/lbs-before
  cleaner-recovery/
    client.c
    probe.py
    client
    kvlds-before
    kvlds-after
```

Build baseline LBS/KVLDS from the pinned upstream source and the comparison KVLDS
from that source with the small `callback_clean` change above. Existing build
caches may be reused. Do not use `NDEBUG`, and do not substitute an unrelated
branch head for the named source pair.

```sh
# From the baseline source checkout:
make -j2 PROGS='lbs kvlds' TESTS= all
# Copy lbs/lbs to run/lbs-recovery/lbs-before and kvlds/kvlds to
# run/cleaner-recovery/kvlds-before.

# From the counterfactual checkout after the one change above:
make -j2 PROGS=kvlds TESTS= all
# Copy kvlds/kvlds to run/cleaner-recovery/kvlds-after.
```

Compile the same client once, from a built source checkout, using absolute paths
for `CLIENT_SOURCE` and `CLIENT_OUTPUT`:

```sh
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -O1 -g \
  -Ilibcperciva/util -Ilibcperciva/events -Ilib/datastruct \
  -Ilib/proto_kvlds -Ilib/util "$CLIENT_SOURCE" \
  liball/liball.a liball/optional_mutex_normal/liball_optional_mutex_normal.a \
  -lcrypto -lm -o "$CLIENT_OUTPUT"
python3 run/cleaner-recovery/probe.py
```

Read `result.json` and the separate `before-kvlds.log` / `after-kvlds.log`; the
launcher records results rather than representing every completed run as a
passing test. The baseline must contain the named abort, and the counterfactual
must exit zero after all exact-value checks, for the result above to be reproduced.

## Retained-byte identities

| File | SHA-256 |
| --- | --- |
| `client.c` | `4a2629ca7980b425459506b4c172463bdf589189fd0508699b900baa6e786166` |
| `probe.py` | `ebdce04c6a1af5f2b542f4f4fc8debc4b27487e080e07eba4bcd5edfa1c2a68c` |
| Baseline cleaner source | `82540e314bf251e669fb8c00ffaf8a331dc57b51f91d34f21ecdb934ab1c7b95` |
| Counterfactual cleaner source | `a9517f239b3677293500645cbe57fe619bf76c5c31c662a6384b91e4a78f13a4` |
| Baseline daemon stderr | `8123d89fdf680bdec41e172105e54ea1fafc23b7008089456e2ea70e8bdb5b51` |
| Recorded `result.json` | `045a089eea92c19ea3cd975447c2344711a1fd2c588773f3fe9168d0c3d02276` |

This evidence supports assessment of the existing bug report; sponsor acceptance,
classification, and payment remain separate decisions. No new bounty is claimed.
