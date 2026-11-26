# Port status (wertrex-port branch)

A personal branch of the open-source project this repository was forked from (see the fork link at the top of the
page; GPLv3, credits in [README.md](README.md) and [LICENSE](LICENSE)). It contains **no game data, keys, executables
or shaders**, and it does not explain how to obtain them. You need your own copy of the Xbox Series package, version 1.0.2.1, as plain files on disk. See
[README.md](README.md) for the build and [AGENTS.md](AGENTS.md) for the repository rules.

**Status: early. Not playable, but the game loads.** On a Windows PC with an RTX 5070 the title boots, runs its menus and the first-run
setup, passes the story load (the console's own loading screens, then the story) and plays the opening of the story: the title animation,
the studio cards and credits, at about 29 frames a second in the intro and about 0.4 to 1 frame a second once the story load is under way, with a
black background behind the credits and no sound. It does not reach the open world yet. The limits measured so far are video memory (about
14 GB wanted on a 12 GB card), the cost of every submission (10 to 16 ms, 50 to 70 submissions a frame in the story) and the early exits
described below. A recent section compares this with the other public ports and what was tried on top of it.

## What is different from upstream

Base: upstream `main` at `77baf90` (25 November 2026). On top of it:

1. `9c6a3dcb` is the upstream pull request #1878 (batch page-copy sources resolved through their CPU aliases),
   taken unchanged.
2. `dc7632ba` is new: the whole-heap rescan of the console descriptor heap is done in budgeted slices
   (`LEONIDA_CONSOLE_RESCAN_US`, 2000 by default, 0 restores the old behaviour).

The rescan used to walk about 870,000 slots of the 27 MB heap in one submission whenever a backing was released or an
alias changed. During the story load that cost 150 to 1,250 ms per submission (up to 2.3 s at most), which made the
title run at 0.3 to 3 frames per second.

## Measured

Windows 11, RTX 5070, Ryzen 9 9950X3D, `LEONIDA_D3D12=vulkan`, `--first-run-ui`, `LEONIDA_TITLE_WINDOWS_MB=512`.

| What | Result |
|---|---|
| Package check and shader translation | 26,503 shaders from `shaders.rpf` plus the 12 in the executable translated; all pass `spirv-val` |
| Boot to the first-run screens | Works; settings persist between launches in `%LOCALAPPDATA%\Leonida\saves` |
| Frame rate on the first-run screens | About 25 frames/s, steady |
| Story load, console-heap phase | 150 to 1,250 ms per submission before, about 6 ms after (max 8 to 13 ms) |
| Story load, overall | 4 to 15 frames/s; the shadow-buffer copy (about 27 ms) and texture refresh (about 32 ms) per submission are next |
| World start | **Not reached.** The story opening plays at about 1 frame/s; no 3D world frame yet |

Raising `LEONIDA_TITLE_WINDOWS_MB` from 64 to 512 moves the failure later but does not remove it: the 64 MiB cap was
reached before the loss (`MiB copied so far: 63`), and with 512 the log reports 511. The cause is not fully pinned
down; see upstream issue #1537.

## Diagnosis of the world-start failure (25 November 2026)

*Superseded: the GPU-loss and early-exit causes were found later and are described in the next sections. Kept for the record.*

Measured with `LEONIDA_DEVICE_FAULT=1` and `--trace`. These are observations, not a fix.

- **The game waits for input.** After the first-run screens it sits on its loading slideshow until the pad gets an A
  press (Enter on the keyboard pad). Without input it idles at 25 frames/s for as long as you leave it.
- **GPU loss.** `VK_ERROR_DEVICE_LOST` is reported from `vkDeviceWaitIdle` in the title staging path. The checkpoint
  names one compute dispatch of the occlusion/culling stage (128x64x1 groups) and the driver lists 136 fault
  addresses inside about 4.7 KB. A timeout of 5 s on queue waits (`LEONIDA_VALUE_WAIT_TIMEOUT_MS`) did not prevent it.
- **The "null write" is the title's own abort.** `+0x23cf683` is a deliberate `mov [0], imm` reached from a watchdog
  thread (about `+0x2215a60`): it aborts when a monitored thread has not reported progress for 300 s. It is not an
  unchecked pointer. On this machine the underlying cause is a thread that stops advancing while the GPU work or
  pipeline builds behind it do not finish.
- **Failed asset reads before it.** The trace shows two `E_DSTORAGE_DECOMPRESSION_ERROR` (`0x89240030`) at about
  102 s. The runtime's own BCPack profile is incomplete (BC7 endpoint/index fields 4228/6144), which is a known
  limitation of the project.
- **A misleading log line fixed here.** `the releaser ... gate Signal ... for 18446744073709551615 ms` was an unsigned
  subtraction wrapping by one tick, not a stall.

## GPU-loss investigation (26 November 2026)

With NVIDIA Nsight Aftermath the runtime names the shader and the instruction behind each `VK_ERROR_DEVICE_LOST`
(`LEONIDA_AFTERMATH=1`, `LEONIDA_AFTERMATH_KEEP_SPIRV=1`; the SDK is not part of the repository). Result of about
twenty runs on one machine:

- The faulting shader differed run to run (vertex and compute programs alike), always an invalid read or write through a
  buffer descriptor (V#) whose base was garbage. Skipping compute programs (`LEONIDA_SKIP_COMPUTE`) only changed which
  one faulted.
- **The cause was stale descriptor data, made worse by the speed settings used earlier the same day.**
  `LEONIDA_REFRESH_PER_FRAME`, `LEONIDA_SHADOW_ALIAS_MS` set high and the asynchronous fills let a shader read a table or
  descriptor that the title had already rewritten. With every shadow refreshed at each submission, the loss went away.
  The earlier statement in this file that the faulting address was "a pointer the title does not own" described the
  symptom, not the cause.
- One real hole was also closed: per-draw V# tables that the title builds in its own memory, mirrored in the arena but
  outside every registered descriptor heap, were never rebased (`scan_title_table` returned early). They are now fixed
  in place (`LEONIDA_ARENA_TABLE_FIX=0` turns it off).
- New diagnostics in the device-fault report: the faulting address as a title address and as `heap_base` + VA, and the
  history of registered GPU backings around it.

Settings that reach the story without losing the GPU (run for 300 s and more): asynchronous pipeline builds
(`LEONIDA_ASYNC_PIPELINES=1`, `LEONIDA_PIPELINE_THREADS=12`), `LEONIDA_TITLE_FILL_ASYNC=1`,
`LEONIDA_GEOMETRY_ASYNC=1`, `LEONIDA_TITLE_TEXTURE_PERIOD_MS=100`, `LEONIDA_SHADOW_ALIAS_MS=250`, and
`LEONIDA_SKIP_COMPUTE` for eight compute programs. Do **not** set `LEONIDA_REFRESH_PER_FRAME`. Rate in that scene: about
1 frame/s, with about 35 submissions per frame costing about 30 ms each (console heaps 9 ms, textures 15 ms, shadows
5 ms). The skipped compute programs are a workaround for earlier faults and are probably not all needed any more.

## Where the time and the memory go (26 November 2026, night)

Measured on one machine (Windows 11, RTX 5070 with 12 GB, Ryzen 9 9950X3D) during the story opening, which runs at about
1 frame/s:

- **Video memory is oversubscribed.** `LEONIDA_VRAM_ACCOUNT=1` counts every device allocation: the process reserves
  about 14 to 15.7 GB on a 12 GB card during the story load, about 9.5 to 10.5 GB of it individual title textures
  (some 2,200 to 2,700 allocations). Windows then keeps about 3 GB in shared system memory, which the GPU reads over PCIe,
  and some allocations fail (`VK_ERROR_OUT_OF_DEVICE_MEMORY`). The demand grows as the title streams more textures.
- **The GPU queue is idle about half of the time** (`LEONIDA_GPU_LATENCY=1`), with queue operations held about 1 s on
  average. A CPU sampler (thread program counters and stacks, symbolised from the PDB) showed one thread at 100% inside
  the NVIDIA shader compiler and the rest waiting.
- **The title thread spent about 72% of its time in `CopyPageMappingsBatchX`**, mapping one 64 KiB page per unmap and
  `MapViewOfFile3`. Runs of consecutive physical pages are now mapped as one view (`LEONIDA_XMEM_BULK_MAP=0` restores the
  old path). Frame rate went from about 0.8 to about 1.0.
- **Pipeline cache.** The session cache was discarded at exit (474 MB over a 256 MB limit) and a title that is busy never
  reaches the exit save. `LEONIDA_PIPELINE_CACHE_BYTES` and `LEONIDA_PIPELINE_CACHE_SAVE_S` keep it; later runs compile less.

### Early exits of the title

Several runs ended with an access violation inside the title. In the clearest case the runtime's own report showed the
page unmapped 1,219 ms before the title read it: the console applies `CopyPageMappingsBatchX` in GPU-queue order and this
runtime applies it when it is called (the same open gap as the upstream notes describe). Calls that unmap pages now leave
them mapped (`LEONIDA_SLOT31_KEEP_MAPPED=0` restores the unmap). One run ran past 270 s with it; longer runs are not yet
confirmed. Two DirectStorage requests (BC7 impostor atlases with no predictor in the local history) used to fail every
run; they are now written as zero blocks and succeed (`LEONIDA_DSTORAGE_LENIENT=0` restores the error).

### Settings

Asynchronous pipeline builds (`LEONIDA_ASYNC_PIPELINES=1`, `LEONIDA_PIPELINE_THREADS=12`), deferred texture and geometry
fills, `LEONIDA_TITLE_TEXTURE_PERIOD_MS=250`, `LEONIDA_SHADOW_ALIAS_MS=250`. Do not set `LEONIDA_REFRESH_PER_FRAME`
(stale descriptors, see above). `LEONIDA_MEMORY_PRIORITY=1` (Vulkan memory priorities) is available but off: with it the
title ended within two minutes in three runs out of three. `LEONIDA_TITLE_MEMORY_MB` and `LEONIDA_SLOT133_IMPORT=0` did not
change the memory or the rate.

### Optional mip skip

`LEONIDA_TEXTURE_SKIP_MIPS=<n>` with `LEONIDA_TEXTURE_SKIP_MIN=<pixels>` makes the images the layer creates for large,
single-layer, sampled 2D textures without their `n` largest mips. With 1 mip from 1,024 pixels the reserved memory was
11.6 GB instead of 15.4 GB at the same point of the load; with 2 mips from 512 pixels it started at 10.6 GB and reached
14.2 GB 100 s later. The frame rate did not clearly change, and two of the three runs with it ended early, so it is off by
default.

### Compute programs that did not translate (26 November 2026)

About 840 of the 2,684 compute programs of the pipeline library, and 2 of the 29 programs of the FidelityFX upscaler DLL
(the first one the title creates), never translated, so the title's `CreateComputePipelineState` calls for them failed
(`0x80070057`, "no translated compute shader for FSPX kind-6 and kind-0") in every run. 839 of the 840 had the same
cause: `llvm-mc` 21.1.8 rejects some words as invalid encodings (a VOP3P `v_dot4_i32_i8`, `0xcc16xxxx`, and one SMEM
form), and a listing with undecodable words is refused by the translator. `tools/shader_disasm.py` now falls back to the
pure-Python RDNA2 decoder when `llvm-mc` leaves invalid words and the decoder leaves fewer
(`LEONIDA_NO_PY_DISASM_FALLBACK=1` turns it off). With it 2,683 of 2,684 library programs and all 29 upscaler programs
translate and pass `spirv-val`. Rebuild the `shaders` stage to pick it up. Effect on the title is being measured; the
Series S profile (`LEONIDA_DEVICE_TYPE=6`, 1080p internal, about 3 GB less video memory at the same point) used to end at
61 s inside the upscaler DLL and now runs for several minutes.

## Intro pacing, story throughput and the other ports (26 November 2026, morning)

Measured on an RTX 5070 (12 GB), 9950X3D, 61 GB of RAM, Series X profile, with the window captured at 30 frames a second.

**The intro stutter was ours.** The title's opening animation (about 19 s) presented 21 new images a second, with freezes of 0.4 to 1.8 s
inside it. The cause was the delays the story needed (`LEONIDA_SHADOW_ALIAS_MS=250`, `LEONIDA_TITLE_TEXTURE_PERIOD_MS=250`, a smaller
`LEONIDA_TITLE_TEXTURE_FILL_MS`): they leave what the title writes up to 250 ms behind. Without them the intro runs nearly smooth.
`LEONIDA_ADAPTIVE_THROTTLE=1` now applies those four values only while the shadow refresh costs more than 10 ms on average (the story load)
and keeps them for at least two minutes (without the hold it flapped on and off dozens of times a minute).

**The story still runs at 0.4 to 1 frame a second.** About 50 to 70 submissions per title frame, each paying a refresh of 10 to 16 ms of
CPU: the console descriptor heap copies 27 MiB of aliased pages at every submission and about 4,700 slots a submission change (more than half of
them are plain data, not descriptors), each fixed eagerly. Changes that went in: the 27 MiB heap is compared in 4 KiB blocks before walking
its slots, the mapped-and-stale texture visit is limited to 6 ms a pass (`LEONIDA_TITLE_EVERY_BUDGET_MS`, it visited about 23,000 textures
and took 53 ms), and `VirtualQuery` is skipped over aliased pages outside the refresh's slice.

**Tried and left off by default**
- `LEONIDA_SHADOW_ALIAS_SLICES=N` copies the aliased pages of the slot-133 shadows in N rotating slices. With 8 the device was lost in a compute
  dispatch; the default is 1 (all, as before). The console descriptor heaps are never sliced: the GPU reads the V#s and T#s in them.
- `LEONIDA_COALESCE_LISTS=N` joins the lists of up to N consecutive `ExecuteCommandLists` of a queue into one replay and one refresh, flushed at a
  Signal, Wait, marker, tile mapping, idle, present or after `LEONIDA_COALESCE_US`. It is the change that would cut the per-submission cost, but
  the device was lost in 2 of 3 runs (a hang in a joined batch). Off by default (0).

**Launcher fact worth knowing:** a run executes `scratch\port\gta6rt.exe`, not the build folder's binary. Copy it after building, or the
change is not tested (several runs this morning measured the old binary).

**Other ports.** Public tracking (PortWatch, tracker data of 26 November) lists builds that play the opening hours at 3 to 16 frames a second,
all built on VIctory (a Rust layer with a native Direct3D 12 backend, not this project): the fastest public one reports about 16 fps on a Radeon RX
7900 XTX with 24 GB of video memory. `leonida-ce`, the other continuation of this project, stops at the GPU loss after the legal terms.
I ran the 7900XTX fork on this machine with the user's own copy: the real 3D intro cutscene renders with sound, at 1 to 2 fps with its defaults
and 5 to 10 with `VICTORY_TEXTURE_BUDGET_MB=2000` (the card stops sitting at its memory limit). Its panel shows most draws dropped for lack of a
host pipeline on a 32-lane GPU, which is the cause of its lighting and texture faults; resolution, the Series S profile (black) and precompiled
pipelines did not help. Its frame replay tool cannot recreate the capture's buffers on this card. Nothing of the game is in this repository.

## Not claimed

No gameplay, no 3D world frame, no controllable character. Figures above are from one machine and a handful of runs
(about 3 minutes each).

## How to reproduce

Follow [docs/first_run_ui.md](docs/first_run_ui.md), then launch with:

```powershell
$env:LEONIDA_D3D12 = 'vulkan'
$env:LEONIDA_TITLE_WINDOWS_MB = '512'
& .\gta6rt.exe --first-run-ui <your package>\game_scarlett_master.exe
```

Nothing is sent online. Do not share the generated work folders, they come from your copy of the game.
