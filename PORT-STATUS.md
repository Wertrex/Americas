# Port status (wertrex-port branch)

A personal branch of the open-source project this repository was forked from (see the fork link at the top of the
page; GPLv3, credits in [README.md](README.md) and [LICENSE](LICENSE)). It contains **no game data, keys, executables
or shaders**, and it does not explain how to obtain them. You need your own copy of the Xbox Series package, version 1.0.2.1, as plain files on disk. See
[README.md](README.md) for the build and [AGENTS.md](AGENTS.md) for the repository rules.

**Status: early. Not playable.** The game starts and reaches its first-run screens. It does not reach the open world.

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
| World start | **Fails.** `VK_ERROR_DEVICE_LOST` between 130 and 190 s, or a null read (`0x8`) at `t+0x108557` |

Raising `LEONIDA_TITLE_WINDOWS_MB` from 64 to 512 moves the failure later but does not remove it: the 64 MiB cap was
reached before the loss (`MiB copied so far: 63`), and with 512 the log reports 511. The cause is not fully pinned
down; see upstream issue #1537.

## Diagnosis of the world-start failure (25 November 2026)

Measured with `LEONIDA_DEVICE_FAULT=1` and `--trace`. These are observations, not a fix.

- **The game waits for input.** After the first-run screens it sits on its loading slideshow until the pad gets an A
  press (Enter on the keyboard pad). Without input it idles at 25 frames/s for as long as you leave it.
- **GPU loss.** `VK_ERROR_DEVICE_LOST` is reported from `vkDeviceWaitIdle` in the title staging path. The checkpoint
  names one compute dispatch of the occlusion/culling stage (128x64x1 groups) and the driver lists 136 fault
  addresses inside about 4.7 KB. A timeout of 5 s on queue waits (`LEONIDA_VALUE_WAIT_TIMEOUT_MS`) did not prevent it.
- **Null write.** With that dispatch skipped (`LEONIDA_SKIP_COMPUTE`, a diagnostic switch) the run no longer loses the
  GPU but the title dies at `game_scarlett_master.exe+0x23cf683` writing to address 0, about 195 s in; the same
  offset in three runs. An earlier run died reading address 8 at `+0x108557`.
- **Failed asset reads before it.** The trace shows two `E_DSTORAGE_DECOMPRESSION_ERROR` (`0x89240030`) at about
  102 s. The runtime's own BCPack profile is incomplete (BC7 endpoint/index fields 4228/6144), which is a known
  limitation of the project.
- **A misleading log line fixed here.** `the releaser ... gate Signal ... for 18446744073709551615 ms` was an unsigned
  subtraction wrapping by one tick, not a stall.

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
