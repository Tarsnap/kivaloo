# Real storage interleaving for issue #320

The existing `main.c` regression models a queue record changing after unlock.
`real_interleaving.c` instead links the actual storage, queue, allocator and disk
implementations. It is an opt-in Linux/GNU-linker reproducer, not another default
test-suite target.

## Executed result, October 4, 2026

The baseline was unmodified upstream commit
`787805e011a62c7b8a87bfae1ed156e5adb4af2c`, tree
`a4716c08e38e181bedd1510d1bef1d4eb4ddaaa6`. The counterfactual used that same
source with only the `storage_read` file-number snapshot already proposed by
this PR: declare `uint64_t fnum`, copy `fs->start` before unlock, and use `fnum`
for both the path and offset. Its explanatory comment differs from this PR's
comment; the executable change is the same. This is not a claim that the entire
older PR head was executed.

Environment: Linux x86_64, kernel 6.18.44, glibc 2.41, GCC 14.2.0
(Debian 14.2.0-19), AddressSanitizer, one reader pthread and the main writer
thread. Compilation succeeded without output for both variants. Each executable
ran once against its own fresh temporary storage directory with a ten-second
process limit.

| Variant | Observed result |
| --- | --- |
| Unmodified upstream | Exit 1; AddressSanitizer reports an 8-byte heap-use-after-free in `storage_read`, `lbs/storage.c:237` |
| Existing file-number snapshot fix | Exit 0; both complete 512-byte blocks match their original bytes, and the next block number is 2 |

The baseline sanitizer stack identifies the real allocation lifetime:

```text
READ of size 8 ... thread T1
    storage_read                 lbs/storage.c:237
    read_first                   real_interleaving.c:39

freed by thread T0:
    realloc
    resize                       libcperciva/datastruct/elasticarray.c:61
    elasticarray_append          libcperciva/datastruct/elasticarray.c:179
    elasticqueue_add             libcperciva/datastruct/elasticqueue.c:60
    storage_write                lbs/storage.c:338
    main                         real_interleaving.c:57
```

The freed region was the 16-byte file-state allocation created by the first
ordinary append. The second append grows the real queue and frees that allocation
before the paused reader reuses its interior pointer. No allocation failure,
malformed input, deleted source block, or substituted queue/disk return value is
required.

Both runs printed:

```text
reader has released its actual rwlock; append block 1 now
ordinary append completed; resume block-0 read
```

Only the repaired run reached:

```text
PASS: both real blocks remain readable with exact original bytes
```

## Scheduling and scope

The GNU linker wraps `pthread_rwlock_unlock`, but the wrapper calls the real
unlock first and returns its actual result. A thread-local flag pauses only the
reader, immediately after it releases the storage lock. Two real pthread barriers
allow the main thread to complete `storage_write(S, 1, 1, block_B)` before the
reader resumes. All storage, file discovery, queue growth, data reads/writes and
fsync calls use repository code and the OS. `storage_init` is called with
`nosync=0`. The fixture supplies two legal blocks: 512 `A` bytes and 512 `B` bytes.

AddressSanitizer supplies its normal instrumented allocator. This deterministically
exposes the allocation move under the selected interleaving; it does not measure
how often an uninstrumented allocator moves the queue in production. This is a
real storage-subsystem reproduction, not a full LBS network-daemon run. It does
not establish an unsanitized crash, stored-data corruption, an exploit, or a
particular bounty classification. The full repository suite was not run.

## Reproduce without changing production files

Save `real_interleaving.c` outside the checkout so the same fixture can be used
against either source revision. From the root of the checkout to examine:

```sh
FIXTURE=/absolute/path/to/real_interleaving.c
RUN=$(mktemp -d)
trap 'rm -rf "$RUN"' EXIT
mkdir "$RUN/storage"

gcc -std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
  -g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread \
  -Ilbs -Ilibcperciva/datastruct -Ilibcperciva/util -Ilib/proto_lbs \
  "$FIXTURE" lbs/storage.c lbs/storage_util.c lbs/storage_findfiles.c \
  lbs/disk.c libcperciva/datastruct/elasticarray.c \
  libcperciva/datastruct/elasticqueue.c libcperciva/datastruct/ptrheap.c \
  libcperciva/util/asprintf.c libcperciva/util/hexify.c \
  libcperciva/util/warnp.c libcperciva/util/noeintr.c \
  -Wl,--wrap=pthread_rwlock_unlock -o "$RUN/repro"

ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  timeout 10s "$RUN/repro" "$RUN/storage"
```

Do not compile this assertion-driven fixture with `NDEBUG`. Leak checking is
explicitly disabled; no leak-free claim is made. The temporary directory must be
new and empty. Run the same command against upstream and the snapshot repair;
only the `storage.c` selection differs in the recorded before/after execution.

## Retained-byte identities

| File from the execution | SHA-256 |
| --- | --- |
| `real_interleaving.c` (unchanged fixture bytes) | `47e4db68d1656dd6eac158dfc49524149b6770ccafc5ce4076f2966a1b51cb90` |
| Counterfactual `storage.c` | `c12db522fc644844efa6f4e5cd982779ff9bd168a06a4e73889a1f2d8c912ae2` |
| Baseline stdout/stderr log | `b185d01df9918bda61b7bd46bf18992797b547bef01e51123232622e96850ce1` |
| Repaired stdout/stderr log | `440dfae0a66bd4934f6fe84096b653adb82fccb5d05db2e5359fb3b53510e774` |

These results supplement the existing first report and source fix. They do not
create a second report or payment claim.
