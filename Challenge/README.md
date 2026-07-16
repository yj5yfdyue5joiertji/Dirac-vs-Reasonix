# 🏆 NexusDB Challenge — The broken engine

**Difficulty:** ⭐⭐⭐⭐⭐
**Language:** C11  
**Codebase:** ~2,500 lines across 13 files (8 .c, 5 .h, + Makefile + tests)

---

## The Situation

You've inherited **NexusDB**, a "distributed task processing engine" written by a
developer who quit halfway through. It *compiles*. It *runs*. But it's riddled
with bugs, missing critical features, and held together by hope.

Your mission: **implement 8 missing features, fix critical bugs, and make
every test pass.** The catch? The codebase has deliberate landmines — memory
corruption, race conditions, off-by-one errors, buffer overflows, and logic
bugs. You can't just implement features; you must *understand* the broken
architecture first.

---

## The Architecture

```
┌─────────────────────────────────────────┐
│                Engine                   │
│  ┌───────────┐  ┌──────────────────────┐│
│  │ Scheduler │  │   Connection Pool    ││
│  │ (buggy)   │  │   (no reap/cleanup)  ││
│  └─────┬─────┘  └──────────┬───────────┘│
│        │                   │            │
│  ┌─────┴───────────────────┴───────────┐│
│  │         Memory Pool                 ││
│  │   (off-by-one, double-free, leak)   ││
│  └─────────────────────────────────────┘│
│  ┌──────────┐  ┌──────────────────────┐ │
│  │  Parser  │  │       Crypto         │ │
│  │ (no val) │  │   (XOR only, weak)   │ │
│  └──────────┘  └──────────────────────┘ │
└─────────────────────────────────────────┘
```

### Files

| File | Purpose | Status |
|------|---------|--------|
| `src/common.h` | Shared types, constants, error codes | ⚠️ Complete but check assumptions |
| `src/memory.h` / `src/memory.c` | Slab allocator with free list | 🐛 Off-by-one bugs, no defrag |
| `src/scheduler.h` / `src/scheduler.c` | Priority task queue | 🐛 Overflow, race condition, no aging |
| `src/parser.h` / `src/parser.c` | Binary protocol parser | 🐛 No validation, buffer overflows |
| `src/crypto.h` / `src/crypto.c` | XOR "encryption" | ❌ Feistel network not implemented |
| `src/net.h` / `src/net.c` | Connection pool | 🐛 No bounds check, no idle reap |
| `src/engine.h` / `src/engine.c` | Orchestration layer | ❌ Batch processing not implemented |
| `src/main.c` | Entry point + built-in tests | ✅ Works (for now) |
| `Makefile` | Build system | ✅ Works |
| `test.sh` | Test suite | ✅ Checks for stubs, crashes, correctness |

---

## Your Mission

### Phase 1: Understand & Fix (Start Here)

The codebase has **critical bugs** that will prevent any new feature from working
correctly. Study each module. The bugs are NOT subtle typos — they're
architectural issues that require understanding the data flow.

**Key bugs to find** (there are more — this is just a starting list):

1. Memory allocator has an off-by-one in `find_free_block()` and `return_block()`
   that corrupts the free list under load.
2. Scheduler queue is NOT circular — `sched_enqueue` writes past `MAX_TASKS`.
3. Scheduler has a race condition between `sched_dequeue`'s check and use.
4. Parser writes past buffer boundaries when `key_len > 64` or `value_len > 512`.
5. `mem_free()` can double-free without detection.
6. `net_close()` leaks sockets and doesn't decrement `count`.
7. `crypto_encrypt()` only does XOR — no Feistel network.
8. `engine_submit()` ignores `conn_id`, so responses never reach clients.
9. No thread synchronization anywhere despite `@threads`-style usage.

### Phase 2: Implement Missing Features

All 8 functions below currently print `NOT IMPLEMENTED` and return an error
sentinel. **You must implement all of them correctly.**

#### 1. `mem_defrag()` — Memory Pool Defragmentation
```
File: src/memory.c
Signature: int mem_defrag(MemoryPool *mp);
```
Coalesce adjacent free blocks into larger contiguous runs. The pool uses a linked
list via `next_block` in `BlockHeader`. After defragmentation, the free list
should contain fewer, larger blocks. Return number of blocks coalesced.

#### 2. `mem_realloc()` — Resize Allocation
```
File: src/memory.c
Signature: void *mem_realloc(MemoryPool *mp, void *ptr, size_t new_size);
```
Resize an existing allocation. If the new size fits in the current block(s),
keep the pointer. If it needs more blocks and adjacent blocks are free, extend.
Otherwise, allocate new, copy data, free old. Return new pointer or NULL.

#### 3. `sched_age_priorities()` — Priority Aging
```
File: src/scheduler.c
Signature: int sched_age_priorities(Scheduler *s, uint64_t current_time_ms, float aging_factor);
```
Tasks that wait too long should gain priority. For every millisecond a task has
waited (current_time_ms - created_at), add `aging_factor / 1000` to its
effective priority. LOW → NORMAL after 5000ms, NORMAL → HIGH after 10000ms.
Return number of tasks whose priority changed.

#### 4. `sched_wait_time()` — Wait Time Query
```
File: src/scheduler.c
Signature: uint64_t sched_wait_time(const Scheduler *s, uint64_t task_id);
```
Return the number of milliseconds since the task was enqueued (created_at).
Return 0 if task not found.

#### 5. `validate_message()` — Message Validation
```
File: src/parser.c
Signature: bool validate_message(const ParsedMessage *msg, char *err_msg, uint32_t err_msg_len);
```
Validate a parsed message:
- Type must be a known `MsgType`
- Key length must be 1-64
- Value length must be 0-512
- TTL must be 0 or >= 100 (no tiny TTLs)
- Checksum must be recalculated and match
Write human-readable error to `err_msg` on failure.

#### 6. `recover_after_error()` — Error Recovery
```
File: src/parser.c
Signature: int32_t recover_after_error(const uint8_t *raw, uint32_t raw_len, uint32_t error_offset);
```
After a parse error at `error_offset`, scan forward to find the next valid
message boundary. A valid boundary starts with a known `MsgType` byte (0x01-0x05,
0xFF). Return the offset of the next valid start, or -1 if none found.

#### 7. `crypto_key_schedule()` — Key Schedule
```
File: src/crypto.c
Signature: uint32_t crypto_key_schedule(const CryptoContext *ctx, uint8_t *round_keys);
```
Derive round keys from the master key. For each round i (0 to CRYPTO_ROUNDS-1):
rotate the master key left by i*2 bits, then XOR with the round constant
`0x9E3779B97F4A7C15 >> (i * 7)`. Store each 8-byte round key in `round_keys`.
Return CRYPTO_ROUNDS.

#### 8. `crypto_feistel_f()` — Feistel Round Function
```
File: src/crypto.c
Signature: uint32_t crypto_feistel_f(uint32_t R, const uint8_t K[8]);
```
Implement the F-function for a Feistel network:
1. Expand 32-bit R to 48 bits: duplicate the 16 middle bits at the ends.
2. XOR with first 48 bits of K (6 bytes: K[0..5]).
3. Split into 8 groups of 6 bits each.
4. Apply S-box: use DES S-box 1 for all 8 groups.
5. Apply P-permutation to the resulting 32 bits.

**Expansion table** (bit positions of R to copy, MSB-first, 1-indexed):
```
32,  1,  2,  3,  4,  5,   4,  5,  6,  7,  8,  9,
 8,  9, 10, 11, 12, 13,  12, 13, 14, 15, 16, 17,
16, 17, 18, 19, 20, 21,  20, 21, 22, 23, 24, 25,
24, 25, 26, 27, 28, 29,  28, 29, 30, 31, 32,  1
```

**DES S-box 1** (row = bits 1+6, col = bits 2-5):
```
14,  4, 13,  1,  2, 15, 11,  8,  3, 10,  6, 12,  5,  9,  0,  7,
 0, 15,  7,  4, 14,  2, 13,  1, 10,  6, 12, 11,  9,  5,  3,  8,
 4,  1, 14,  8, 13,  6,  2, 11, 15, 12,  9,  7,  3, 10,  5,  0,
15, 12,  8,  2,  4,  9,  1,  7,  5, 11,  3, 14, 10,  0,  6, 13
```

**P-permutation** (maps bit positions 1-32 to new positions 1-32):
```
16,  7, 20, 21, 29, 12, 28, 17,  1, 15, 23, 26,  5, 18, 31, 10,
 2,  8, 24, 14, 32, 27,  3,  9, 19, 13, 30,  6, 22, 11,  4, 25
```

### Phase 3: Integration Features

#### 9. `engine_process_batch()` — Batch Encrypted Processing
```
File: src/engine.c
```
Process a batch of encrypted task buffers: decrypt, parse, validate, enqueue.
Return number of successfully enqueued tasks.

#### 10. `engine_defrag()` — Trigger Defragmentation
```
File: src/engine.c
```
Trigger memory pool defragmentation. Pause task processing during defrag.
Return blocks coalesced.

---

## Success Criteria

Run `./test.sh`. All tests must pass:
- ✅ Build succeeds
- ✅ Binary runs without crash
- ✅ Zero "NOT IMPLEMENTED" messages
- ✅ Basic submit works
- ✅ Crypto roundtrip correct
- ✅ Memory allocation test passes
- ✅ Scheduler stress test passes
- ✅ No segfaults

**Bonus:** Run `make check` and reduce compiler warnings to zero.
**Bonus:** Run `make memcheck` and fix all valgrind errors.

---

## Hints

1. **Read ALL the .h files first.** They define the interfaces and data structures.
2. **The off-by-one in memory.c** is in the relationship between `block_id`
   (1-indexed) and `headers[]` (0-indexed). The code is inconsistently wrong.
3. **The scheduler needs a mutex** if you're using threads. But for the basic
   fix, making the queue circular is higher priority.
4. **The parser's `serialize_message` recalculates CRC** over the wrong range.
   `validate_message` must recalculate the CRC correctly to match.
5. **For the Feistel function**, implement the expansion, XOR, S-box lookup,
   and P-permutation exactly as specified. Bit manipulation in C requires
   careful masking and shifting.
6. **The key schedule** uses big-endian rotation. The constant `0x9E3779B97F4A7C15`
   is the golden ratio φ × 2^64.

---

## Deliverables

Modify the source files in `src/` directly. Do NOT create new files unless
absolutely necessary. The test script and Makefile should work without
modification.

**Good luck. You'll need it.**
