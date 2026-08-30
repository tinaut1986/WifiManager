# WiFiManager+

A 3DS homebrew for managing your console's Wi-Fi configuration slots.

> **This is a fork.** WiFiManager+ is based on
> [LiquidFenrir's **WifiManager**](https://github.com/LiquidFenrir/WifiManager)
> and is not the original project. It keeps the same purpose — backing up and
> restoring the three system Wi-Fi slots — but the interface has been rebuilt on
> citro2d and it adds QR import/share and in-app editing.

## What's different from the original

- Rebuilt UI (citro2d): dual-screen, cards, badges, context menus, toasts.
- **Scan a Wi-Fi QR code** with the outer camera and save it or write it to a slot.
- **Share** any saved network or console slot **as a Wi-Fi QR code**.
- **Edit** a saved backup (SSID / security / password) and rename its alias.
- Backups are named automatically from the SSID; you're only asked for an
  optional alias. The save date is stored in the file and shown in the list.
  User-facing names may repeat freely.

## Features

- Back up any console slot to a `.bin` file on the SD card.
- Restore / copy a backup into any of the three console slots.
- QR support: the standardised
  `WIFI:S:<ssid>;T:<WPA|WEP|nopass>;P:<pass>;H:<true|false>;;` payload
  (Android/iOS/router style) **and** a plain-text fallback
  (`SSID`, or `SSID` + newline + `password`).
- Show / hide passwords.

## Controls

| Input | Action |
|-------|--------|
| D-pad Left/Right | Select console slot |
| D-pad Up/Down | Move through the list |
| A | Options for the selected entry (restore / share QR / edit / rename / back up) |
| X | Delete the selected backup |
| Y | Show / hide passwords |
| L | Open the QR scanner |
| SELECT or R | Help |
| START | Quit |

In the editor: D-pad moves between fields, A edits a field, X confirms, B cancels.

Backups live in `sdmc:/3ds/WifiManager/` (kept at the original path so existing
backups still load).

## Building

Requires [devkitPro](https://devkitpro.org/) with `devkitARM`, `libctru` and
`citro2d`/`citro3d` (`pacman -S 3ds-dev`).

```
make
```

Produces `out/WiFiManagerPlus.3dsx` (and `out/WiFiManagerPlus.cia` when `makerom`
and `bannertool` are on `PATH`).

### Banner / icon

`python3 meta/make_art.py` (needs Pillow) regenerates `meta/icon.png` (48×48)
and `meta/banner.png` (256×128) — a *flat* banner: no real stereoscopic depth or
animation. A true 3D/animated Home-Menu banner is an authored **CGFX** scene
(layers at different Z, animation tracks) that can't be produced from a plain
image. If you make one, drop it at `meta/banner.cgfx` and the Makefile passes it
to `bannertool -ci` automatically.

## Third-party code

- Original project: [WifiManager](https://github.com/LiquidFenrir/WifiManager)
  by LiquidFenrir (MIT).
- `source/checksum.h` / `source/crc16.c` — from
  [libcrc 2.0](https://github.com/lammertb/libcrc/tree/v2.0) by lammertb (MIT),
  trimmed to the CRC-16 routine used to re-checksum a Wi-Fi slot.
- `source/quirc/` — [quirc](https://github.com/dlbeer/quirc) by Daniel Beer
  (ISC), QR decoding from camera frames.
- `source/qrcodegen/` — [QR Code generator](https://github.com/nayuki/QR-Code-generator)
  by Project Nayuki (MIT), QR encoding for the share screen.

## License

MIT, like the original — see `LICENSE.txt`.
