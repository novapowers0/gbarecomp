# DEBUG LOOP (EXECUTION CONTRACT)

Use the part of this workflow that matches the investigation. Live guest-state
inspection uses TCP; see `TCP.md`. Existing offline fixtures, explicit snapshot
exports, compiler/disassembly checks and bounded phase/performance counters are
also supported. Keep expensive diagnostic recording out of timing samples.
The current `PRINCIPLES.md`, `HLE.md` and shared optimization policy supersede
historical blanket gates below. A presentation optimization does not require
requalifying unrelated BIOS, CPU or cartridge behavior.

---

# RULE 0 — TOOL VALIDATION (FIRST USE)

On the first use of any tool:

- Cross-check against another source (oracle, disassembler, raw ROM
  dump).
- Verify structure and content.
- Confirm assumptions.
- Never trust output blindly.

If not validated → all reasoning is INVALID.

---

# RULE 0a — DISPATCH MISS CHECK (every run, before any debugging)

Inspect dispatch/coverage evidence for every acceptance run, including
`dispatch_misses.log` when present and the runtime's final counters.

- If non-empty, retain the evidence and identify the missing control-flow or
  runtime contract. Review a metadata proposal before changing `game.toml`;
  do not blindly add PCs or hand-edit generated bodies.
- A strict static acceptance run must abort on a miss and finish with zero
  interpreted/healed execution. Discovery builds may bridge loudly under
  the current coverage policy; report their coverage honestly.

A coverage gap does not invalidate unrelated host-only measurements, but it
must not be hidden or described as fully static qualification.

---

# RULE 0c — BIOS SYNC

For BIOS/CPU timing comparisons against mGBA or NanoBoyAdvance, use matching
boot modes and actual qualified BIOS implementations. Record meaningful sync
points such as BIOS entry and cartridge handoff. A build-selected BIOS HLE
service is assessed against its documented caller contract; it need not
reproduce every original instruction or stall. Boot-intro selection is separate
from SWI service selection.

Symptoms that mean RULE 0c is violated:
- Native skips the Nintendo logo intro but oracle plays it.
- Native SWI handler returns instantly; oracle takes real cycles.
- Cartridge code on native starts before BIOS has cleared IWRAM.

If boot modes or BIOS semantics differ unintentionally, fix or isolate that
mismatch before claiming an exact downstream differential. Presentation-only
timing from an existing matched checkpoint remains a separate valid experiment.

---

# RULE 0d — Investigate BIOS defects in their affected scope

An unexpected BIOS-intro difference is a correctness finding. Retain it,
identify the first relevant divergence and fix the affected path. Exact LLE
claims require exactness; a declared HLE approximation uses its caller contract.

Symptoms that mean RULE 0d is violated:
- `diff_frame.py` reports any byte diff in OAM/PAL/VRAM/IWRAM for
  any frame between boot and the post-intro idle state.
- The live BIOS window shows a visibly different intro animation
  than mGBA renders.
- The startup chime sounds different than mGBA's chime, or is
  silent on native.

The former global "BIOS flawless before all ROM work" gate is obsolete.
Maintain the functioning real-BIOS LLE reference and record its limitations;
do not block unrelated game, generator or host-presentation work on a known
isolated defect. This does not permit hiding a BIOS defect with fabricated HLE
success or weakening netplay determinism.

---

# RULE 0b — RING-BUFFER FIRST

Probes **query** the always-on ring buffer for the window of
interest. Probes **never** arm-and-record-then-run. If the event you
need isn't in the ring, extend the ring (add the new event class to
the always-on capture path). Then query.

If you find yourself reasoning "the events must have happened before
I attached" — STOP. The ring isn't covering enough.

Pause-and-step is a control-plane primitive for a human at a
debugger, not a way to synchronize two observers. Two-observer
synchronization is free-run + ring query + diff.

---

# TWO DEBUGGING SURFACES

Pick the one that matches the question:

| Surface | When |
|---------|------|
| **Frame ring buffer** (per-frame snapshots) — `get_frame`, `frame_range`, `frame_timeseries`, `history` | You need a snapshot comparison across frames, or the question is "what did frame N look like?" |
| **Reverse debugger** (`rdb_*`, per-store / per-block / per-call rings) | "Which instruction wrote VRAM addr X?" / "Break inside this function" / "Reconstruct IWRAM at an arbitrary past block." |

For "recomp ≠ oracle at byte X at frame Y": walk the frame ring
backwards to find the **first** frame where X diverges, then switch
to `rdb_*` to find the first write to X that produced the wrong
value, then trace that writer.

---

# THE LOOP

0. Tool validation (first use only)
0a. Dispatch-miss check
0b. Confirm the ring covers what you need; extend if not
1. **Sync state** — establish what "the same point in execution"
   means across native and oracle (VBlank count, IRQ count, BIOS
   handoff PC — not bare frame number).
2. **Dump full state** — native and oracle, from the always-on ring
   (`get_frame` / `frame_range` / `frame_timeseries`).
3. **Diff bytes** — find bytes / registers / IO regs that differ.
4. **Find FIRST divergence** — walk the ring backwards.
5. **Trace the writer** — function + instruction + call path. For
   "which store produced this byte":
   - Arm `rdb_range` covering the suspect address.
   - Run past the divergence.
   - `rdb_dump` — the last entry at that address is the bug writer.
   - For block context: `trace_blocks_range` around the writer's PC
     and `get_block_trace` for register state at each block entry.
   - To park right before the bad write: `rdb_watch_add addr=<a>
     val=<bad>`.
6. **Classify** — codegen bug / runtime bug / scheduler-timing bug /
   IO bug / DMA bug / IRQ bug / PPU bug / save-chip bug / config bug.
7. **Fix the tool** — recompiler, runtime, or TCP tooling. Never
   edit generated output.

For a guest correctness divergence, retain the relevant sync, first-difference
and writer evidence. Use a focused contract/measurement workflow for a host-only
optimization rather than inventing a guest write divergence where none exists.

---

# HARD RULES

- **Live guest inspection uses structured tools.** Extend TCP/rings when needed.
  Bounded startup/exit counters, phase CSVs and isolated test diagnostics are
  allowed; avoid unbounded hot-loop logging and keep it out of timings.
- **No hand-editing generated output.** Fix the generator / runtime /
  game config and regenerate.
- **No guessing.** Every claim must cite measured data, oracle
  comparison, or ROM disassembly.
- **State which surface and which index.** "Per the frame ring at
  f=412 (vblank=410)" or "per `rdb_dump` at block 1,258,944." Not
  "around frame 400."
- **Walk backwards to the first divergence.** Later differences are
  consequences; only the first has a root cause.
- **One mode at a time.** ARM and THUMB share state. When the bug
  involves interworking, dump both the pre-`BX` mode and the
  post-`BX` mode at the same ring index.

---

# WHEN DOCUMENTING A FINDING

Every debugging response must at minimum state:

1. Target behavior being verified.
2. Oracle (mGBA TCP / NanoBoyAdvance / native build TCP / ROM via
   disassembler).
3. Sync point (hardware-event count, not bare frame).
4. Diff (subsystem, address, expected, actual).
5. First divergence (frame index or block index — measured, not
   eyeballed).
6. Writer (function + PC + call path + ARM/THUMB mode).
7. Classification.
8. Minimal fix proposal (in the recompiler / runtime / config —
   NEVER in generated code).
9. Re-test plan.

These fields describe guest correctness findings. For host/performance work,
record the supported contract, exact build/input identity, useful work measured,
relevant focused checks and limitations instead.

---

# WHAT GOES IN THE ALWAYS-ON RING

Every frame snapshot must include enough to reconstruct the divergent
window after the fact:

- CPU: R0..R15 in current mode, CPSR, banked SP/LR for all modes,
  SPSR_irq/svc/fiq, current mode flag, current ARM/THUMB flag.
- PPU: DISPCNT, DISPSTAT, VCOUNT, all BGxCNT, all BGxHOFS/VOFS,
  affine BG params, WIN regs, MOSAIC, BLDCNT/BLDALPHA/BLDY.
- DMA: each channel SAD/DAD/CNT/active state.
- Timers: each timer counter, reload, CNT, active.
- IRQ: IE, IF, IME.
- Sound: SOUNDCNT_L/H/X, FIFO levels, current channel state.
- Save chip: detected type, current command/sector state.
- IO snapshot: the whole 0x04000000 page slice that's defined.
- Memory: full IWRAM (32 KB), full PAL (1 KB), full OAM (1 KB), full
  VRAM (96 KB). EWRAM (256 KB) is sampled per-region or per-anchor;
  full EWRAM lives in anchor snapshots, not every frame.
- Last function entered, last SWI, last DMA fired, last IRQ taken.
- Up to N (configurable) verify-mode diffs vs oracle for the frame.

While the diagnostic surface is enabled, retain bounded history for queries
instead of requiring a late arm-and-record sequence. Heavy snapshot/payload
recording may be gated for normal play and disabled in performance measurements;
retain cheap coverage counters and an explicit way to enable detailed capture.
Report when history is unavailable. Netplay's canonical rollback/state history
is a separate required service and cannot be disabled as a diagnostic shortcut.
