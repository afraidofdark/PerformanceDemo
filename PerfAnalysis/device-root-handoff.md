# SM-G950F (dreamlte) recovery handoff

Date: 2026-11-01 (late) / 2026-11-02
Device: Samsung Galaxy S8 SM-G950F, Exynos 8895 (Mali-G71), stock Pie G950FXXUCDZE7

## Current state (end of session)

- Device is in a boot loop: Samsung logo -> reset, falls back to stock Android recovery.
- Flashed partitions (all from the official G950FXXUCDZE7 Pie package):
  BOOT (stock), RECOVERY (stock), SYSTEM (stock), USERDATA (factory image), CACHE (HOME_CSC).
- NOT flashed: BL (sboot.bin, param.bin, up_param.bin, cm.bin), CP (modem.bin, modem_debug.bin), HIDDEN.
- Download mode last reported: `OEM LOCK: OFF`, `FRP LOCK: OFF`, `KG STATE: Checking`,
  `CURRENT BINARY: Custom`.
- `/data` was formatted and later factory reset several times; `pm clear` state is gone.

## Why the root attempt failed

1. Only partial flashes were done over heimdall; BL/CP were never written, so the bootloader
   bookkeeping ("current binary", integrity) never reached a clean official state.
2. Knox Guard (KG) went from `Normal` (the state in which TWRP + LineageOS worked in the past)
   to `Checking`/`Prenormal` after `format data` and the factory reset. In that state the
   bootloader refuses to boot unsigned BOOT/RECOVERY images.
3. Three root routes were tried:
   - TWRP + Magisk in recovery: booted once, then KG refused the next boot.
   - Magisk `boot_patch.sh` on Linux (LEGACYSAR false/true): stuck at Samsung logo.
   - Magisk app "Select and Patch a File": "verification failed" screen.
   All three hit the same gate: KG state, not the images themselves.

## Next step (Windows + Odin, recommended)

1. Copy `~/Downloads/SAMFW.COM_SM-G950F_EUR_G950FXXUCDZE7_fac.zip` to the Windows machine.
2. Unzip it. It contains BL/AP/CP/HOME_CSC/CSC `.tar.md5` files.
3. Install Samsung USB driver + Odin3 on Windows.
4. Phone: hold Power + VolDown + Bixby ~7 s -> blue warning -> VolUp -> "Downloading...".
5. Odin: `BL` = BL_*.tar.md5, `AP` = AP_*.tar.md5, `CP` = CP_*.tar.md5,
   `CSC` = CSC_*.tar.md5 (CSC wipes data, HOME_CSC keeps it). Leave other slots empty.
   Options: F.Reset Time ON, Auto Reboot ON. Do NOT tick "NAND Erase"/"Repartition".
6. Start. Expect a reboot into the setup wizard after ~10 minutes.
7. Let the phone stay online ~30 minutes, then check `KG STATE` in download mode.
   It should read `Normal`.

## Root, only after KG reads Normal

1. Download mode, then from Linux:
   `heimdall flash --RECOVERY twrp-dreamlte.img --no-reboot`
2. Boot TWRP with Power + VolUp + Bixby and let it run `format data` and
   `twrp install /tmp/magisk.zip` (Magisk 31.1).
3. Reboot and check `adb shell su -c id`.

If `KG STATE` is not `Normal`, root will keep failing the same way. Stop there.

## Local files kept for tomorrow

- `~/tk-root/twrp-dreamlte.img`              TWRP 3.7.0_9-0 for dreamlte
- `~/tk-root/Magisk-v31.1.apk`               Magisk installer/app
- `~/tk-root/Magisk-v26.4.apk`               older fallback
- `~/tk-root/magisk_patched.img`             boot image patched by the on-device Magisk app
- `~/tk-root/new-boot-legacysar.img`         Linux-side patch with LEGACYSAR=true
- `~/tk-root/fw/`                            extracted firmware pieces + `pit.txt` (30 partitions)
  - `boot.img`, `recovery.img`, `system.img`, `userdata.img`, `cache.img`
  - `AP/BL/CP/HOME_CSC` tars, original samfw zip stays in `~/Downloads`

Partition mapping used with heimdall (from `pit.txt`):
`sboot.bin -> BOOTLOADER`, `param.bin -> PARAM`, `up_param.bin -> UP_PARAM`, `cm.bin -> CM`,
`modem.bin -> RADIO`, `boot.img -> BOOT`, `recovery.img -> RECOVERY`, `system.img -> SYSTEM`,
`userdata.img -> USERDATA`, `cache.img -> CACHE`.

## Unrelated work that is still valid

The performance work on PerformanceDemo is untouched by all of this and was committed
separately: present path (window-sized present target + blit), bloom at quarter resolution,
render scale 65 percent, shadows/SSAO off, malioc shader costing records. Raw numbers from
this device (SM-G950F, Mali-G71): 28 ms per frame at 65 percent scale (about 35 fps), with a
13-15 ms fixed floor for composite plus blit at the full 2.4 MP window.
