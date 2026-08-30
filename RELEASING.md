# Branching & releases

This fork uses the same lightweight flow as the upstream `mzm` 3DS port.

## Branches

| Branch | Purpose |
|--------|---------|
| `main` | Stable trunk. Always builds. Only fast-moving via merges from release branches / small fixes. |
| `release/vX.Y.Z` | Integration branch for the next version. Feature and fix branches are merged **here** first. |
| `feat/*`, `fix/*`, `claude/*`, … | Short-lived work branches, one topic each, branched off `main` (or off the current `release/*` when the work targets that release). |

## Version string

`make print-version` derives it from git (see the header of the `Makefile`):

- On an exact tag → the tag, e.g. `v0.4.0`.
- On `release/vX.Y.Z` → `X.Y.Z-dev.<rel>[.<dev>]+<hash>`
  - `<rel>` = commits on the release branch since `origin/main`
  - `<dev>` = extra commits stacked on top (omitted when 0)
- Anywhere else → `git describe --tags --always`.

A branch based on a `release/*` branch inherits that release's version. Override
with `make VERSION=...` if needed. The string is baked into the SMDH description,
the CIA version (`-major/-minor/-micro`) and `APP_VERSION` (shown on the Help
screen).

## Cutting a release

1. Land everything for the version on `release/vX.Y.Z` (via PRs).
2. Make sure it builds: `make clean && make` (produces `out/WiFiManagerPlus.{3dsx,cia}`).
3. Fast-forward / merge `release/vX.Y.Z` into `main`.
4. Tag the tip and push the tag:
   ```
   git tag v0.4.0
   git push origin v0.4.0
   ```
5. The **Build 3DS Release** workflow (`.github/workflows/build-release.yml`)
   then builds in the `devkitpro/devkitarm` container and publishes a GitHub
   Release with:
   - `WiFiManagerPlus-v0.4.0.cia` and `.3dsx`
   - `QR-v0.4.0.png` — a QR of the `.cia` download URL (scannable with a phone
     or Universal-Updater)
   - an auto-generated changelog since the previous tag.
6. Open the next `release/vX.Y.(Z+1)` branch off `main`.

## CI

- `build.yml` — builds the `.3dsx` + `.cia` on every push to `main` / `release/**`
  and on PRs, and uploads them as workflow artifacts ("automatic CIAs").
- `build-release.yml` — the tagged release job above (also runnable manually via
  *workflow_dispatch*, which produces a prerelease).

Both use the `bannertool` / `makerom` binaries vendored in `tools/bin/`.

## Local deploy to a 3DS

```
make ftp                    # -> ftp://192.168.1.137:5000  (FTPD on the console)
make ftp FTP_HOST=10.0.0.5 FTP_PORT=5000
```
Uploads `cias/WiFiManagerPlus-<VERSION>.cia` and `3ds/WiFiManagerPlus/…`.
