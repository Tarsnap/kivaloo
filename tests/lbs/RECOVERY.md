# Actual LBS recovery after a rejected APPEND

This is execution evidence for existing issue #330 and PR #331, recorded on
October 4, 2026. It is not a second report or an assumed bounty classification.

## Exact source and execution scope

The daemon baseline is unmodified upstream
`787805e011a62c7b8a87bfae1ed156e5adb4af2c`. The second daemon uses that same
source with only this PR's existing three `goto drop2` changes and the matching
`D->npending -= 1` rejection unwind in `lbs/dispatch.c`.

Both variants use the identical maintained `tests/lbs/main.c` from this PR's
`65c98afce90ca6e3e500754aa9289dbf2267a3b6`: Git blob
`9bad0cba6dc8738e0819908a7ffe17cabc94e2ae`. Its source bytes were checked before
compilation, and the same compiled `test_lbs` executable was used for both
variants. Only its existing `params` and `badappend` modes ran. This does not
claim execution of the whole older PR tree, `tests/lbs/test.sh`, or the full
repository suite.

The binaries used the repository's Makefiles, actual C protocol/CRC code,
network/event loop, worker threads, socket transport and storage implementation.
There are no mocked network callbacks, queues, counters or disk operations, and
no sanitizer or allocation-failure injection in this check.

## Observed results

Environment: Linux x86_64, kernel 6.18.44, glibc 2.41, GCC 14.2.0. Each daemon
was launched once against its own empty temporary directory and Unix socket,
with `-b 512 -n 1`. A new client process was used for each row. Every client had
a two-second process limit.

| Ordered operation | Baseline | Existing count-unwind fix |
| --- | --- | --- |
| Initial valid `params` client | Exit 0, 0.001599 s | Exit 0, 0.001739 s |
| `badappend` client: one 511-byte block against a 512-byte server | Timed out, 2.002778 s | Exit 0, 0.001156 s; client confirmed rejection |
| Separate subsequent valid `params` client | Timed out, 2.003059 s | Exit 0, 0.001350 s |
| Daemon still existed after these operations | Yes | Yes |
| Files written in storage directory | None | None |

These are single observed wall times, not benchmark distributions or an SLA.
`badappend` returning zero means that the maintained client observed the
expected failed request; it does not mean the invalid block was accepted.

The baseline's first valid client establishes that the daemon and exact client
can communicate normally. The later valid client is a distinct process and
connection, created after the offending client was terminated by its timeout.
It still receives no PARAMS reply. The repaired daemon closes the rejected
client normally and answers the next connection without a restart.

The stored-data directory remains empty in both variants. This confirms that
the invalid APPEND did not write a block; this run does not establish data loss
or corruption. Both daemons were terminated and reaped using their own recorded
PIDs after the check, and both temporary directories were removed.

## Why this matches the source defect

`gotrequest` increments `npending` before it checks the implied APPEND block
length. The baseline rejection path frees that APPEND buffer and request but
never decrements the count. It also stops reading the connection. No worker
operation was launched which could later supply the missing decrement, so
`dispatch_alive` remains true and the main loop never returns to accepting a
new connection. The fix restores the count only on post-increment rejection
paths; the pre-increment parse-failure path is unchanged.

The two PARAMS/PARAMS2-during-write rejection sites share the fix, but those
interleavings were not executed in this check. The measured result is the
APPEND block-length mismatch and subsequent-client recovery.

## Focused reproduction

Build the existing targets, not the entire project or test suite:

```sh
make -j2 PROGS=lbs TESTS=tests/lbs all
```

The `tests/lbs/main.c` used for a baseline checkout must be copied from the PR
revision above; that gives both daemons the same maintained probe modes while
leaving baseline daemon source unchanged. Use the same probe executable for
both runs.

Start one variant with its own fresh temporary directory:

```sh
R=$(mktemp -d)
mkdir "$R/storage"
./lbs/lbs -s "$R/lbs.sock" -d "$R/storage" -b 512 -n 1 -p "$R/lbs.pid"
# Cleanup only this disposable daemon, never a shared instance.
trap 'kill "$(cat "$R/lbs.pid")" 2>/dev/null; rm -rf "$R"' EXIT

timeout 2s ./tests/lbs/test_lbs "$R/lbs.sock" params
printf 'initial params exit: %s\n' "$?"
timeout 2s ./tests/lbs/test_lbs "$R/lbs.sock" badappend
printf 'badappend exit: %s\n' "$?"
timeout 2s ./tests/lbs/test_lbs "$R/lbs.sock" params
printf 'subsequent params exit: %s\n' "$?"
```

On the baseline, the last two commands time out. On the repaired daemon all
three exit zero. The recorded execution used Python's `subprocess.run` timeout
rather than the shell `timeout` utility; timed-out results are recorded as
`timed_out=true`, not as a fabricated daemon exit code. Both client processes
are killed and reaped on timeout.

This is evidence of loss of service to a later valid client, not a measured
process crash. Sponsor eligibility and classification remain separate from the
technical result.
