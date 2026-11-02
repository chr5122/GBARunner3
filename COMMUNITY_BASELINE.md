# Community slot2patch integration: H1 + SR2

This source line integrates the community PATCH B3 prerequisites, H1 shared HBlank/window-DMA maintenance, and SR2 high-ROM SWI halfword selection. It is based on Wokann `cache-hicode/slot2patch` at `f8ac25cc8c79ec5a4c624cbd94eeed9e2d1842c9`, in turn based on Gericom and the original contributors. Existing notices and licenses are preserved.

The integration was requested by the community project owner on 2026-11-02 after reporting separate hardware success for H1 and SR2. The combined guard also includes a coherent-scanline-sample refinement found during integration. The newly combined executable has not yet been tested on physical hardware; the current evidence is recorded separately from the individual hardware feedback.

## What is included

- Existing D2 automatic PRE generation, runtime read-only/log-free behavior, optional launch placeholder, G1 audio wrapping, R1 read-error propagation, 2KiB alignment and B3 cached Thumb BX fast path, inherited from the frozen community B3 source.
- H1: frame-start stale HBlank filtering and restricted DMA0 WIN0H/WIN1H maintenance during 32-byte pieces of a Slot-2 ROM cache fill. IRQs remain masked; the block stays unpublished until complete. The underlying timing path is shared throughout gameplay, not cave-specific. Cave circles are the measured visual fixture; unrelated effects are not claimed fixed.
- SR2: select the correct halfword before extracting the cached high-ROM SWI immediate. The SWI1 preceding Emerald's SoftReset must not be mis-dispatched as save SWI 0x8D.

A2 and the failed SR1 SoftReset interception are not included. Save/Flash code is unchanged relative to the H1 parent. This branch is separate from the small independent upstream PR #9, which intentionally excludes other community features.

## Build

Check out this branch with the `code/libs/libtwl` submodule. Use the project's devkitARM/libnds toolchain and run `make -C code` sequentially. The bootstrap consumes generated core-size data; after changing the core, ensure its `arm9/build/main.o` and bootstrap ELF/NDS rebuild as well. The verified local build uses the inherited 228-byte legacy diagnostic-tail omission; it is preserved rather than expanded.

Combined NDS SHA256: `1aa3ab06bd34c140c260bd833f3777714324ec1b50e24d8e0513628e16125199`, 228864 bytes. Toolchain/packaging metadata can affect full-file reproducibility. All RAM/data symbol addresses match H1 and ARM7 is byte-identical. The first naive combination exposed one missed HBlank in 446 frames: separate line/status reads could straddle a scanline. The integrated guard now samples DISPSTAT/VCOUNT together, while retaining the restricted H1 DMA behavior. This adds one ARM instruction and relocates subsequent code, so the final binary is not a 16-byte patch. The build also changes two nonexecuting NDS metadata fields totaling 40 bytes. ITCM ends at 0x7FF0.

Full evidence and the current testing boundary: [community mainline record](https://github.com/chr5122/GBARunner3-slot2patch-review/tree/main/h1_sr2_mainline). This is a community integration, not a claim that Wokann or Gericom has merged it. No game ROM, BIOS, save, PATCH/PRE or NDS binary is included in this source tree.
