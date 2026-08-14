# AcreetionOS Media Writer

AcreetionOS Media Writer is a tool that helps users write AcreetionOS images onto USB flash drives. It can automatically download the required image and write it in a `dd`-like fashion using direct device access. Because this overwrites the drive's partition layout, it also provides a **Restore** feature to reformat the drive back to a standard single-partition layout when you are done.

AcreetionOS Media Writer runs on Linux, Windows, and macOS.

This project is a fork of [Fedora Media Writer](https://github.com/FedoraQt/MediaWriter) by the Fedora Project. We are grateful for their original work and continue to build upon it under the terms of the GPLv2+ license.

![AcreetionOS Media Writer running](/dist/screenshots/linux_main.png)

## Table of Contents

- [Downloads](#downloads)
- [Building](#building)
- [Publishing a release](#publishing-a-release)
- [Translation](#translation)
- [Troubleshooting](#troubleshooting)
- [Security & Privacy](#security--privacy)

## Flatpak

A Flatpak bundle is attached to every GitHub release as
`org.acreetionos.MediaWriter.flatpak` — download it from the
[releases page](https://github.com/spivanatalie64/AcreetionMediaWriter/releases/latest)
and install it with:

```bash
flatpak install --user ./org.acreetionos.MediaWriter.flatpak
```

## Downloads

Pre-built releases are published on the [GitHub Releases](https://github.com/spivanatalie64/AcreetionMediaWriter/releases) page. Every push to `main` triggers a CI build of all package types; artifacts are uploaded as workflow run artifacts for development builds and attached to the release for tagged versions.

| Package | Format | Platform |
|---------|--------|----------|
| Linux | `.AppImage` | x86_64 |
| Linux | `.deb` | x86_64 (Debian/Ubuntu) |
| Linux | `.rpm` | x86_64 (Fedora/RHEL) |
| Linux | `.pkg.tar.zst` | x86_64 (Arch Linux) |
| Linux | `.flatpak` | all — via [FlatFree](https://natalie.acreetionos.org/FlatFree) |
| macOS | `.dmg` | x86_64 |
| macOS | `.dmg` | arm64 (Apple Silicon) |
| Windows | `.exe` (NSIS installer) | x86_64 |
| Windows | `.zip` (portable) | x86_64 |

Arch Linux users can install from the AUR:

- [acreetionos-mediawriter](https://aur.archlinux.org/packages/acreetionos-mediawriter) — builds from source
- [acreetionos-mediawriter-bin](https://aur.archlinux.org/packages/acreetionos-mediawriter-bin) — pre-built binary from GitHub releases

```bash
# With an AUR helper (e.g. paru or yay)
paru -S acreetionos-mediawriter
# or
yay -S acreetionos-mediawriter
```

```bash
# Pre-built binary (no compilation)
paru -S acreetionos-mediawriter-bin
# or
yay -S acreetionos-mediawriter-bin
```

```bash
# Manual build from source
git clone https://aur.archlinux.org/acreetionos-mediawriter.git
cd acreetionos-mediawriter
makepkg -si
```

## Building

You can build AcreetionOS Media Writer using the standard Qt `cmake` build system. For a detailed look at how releases are composed, see the [GitHub Actions configuration](https://github.com/spivanatalie64/AcreetionMediaWriter/tree/main/.github/workflows).

### Requirements

| Platform | Dependencies |
|----------|-------------|
| Linux    | `udisks2` or `storaged`, `xz-libs` |
| Windows  | `xz-libs` |
| macOS    | `xz-libs` |

### Linux

Specify the install prefix using the `-DCMAKE_INSTALL_PREFIX` cmake option (default is `/usr/local`):

```
cmake [OPTIONS] .
```

The main binary `mediawriter` will be installed to `$PREFIX/bin` and the helper binary to `$PREFIX/libexec/mediawriter/helper`.

### Windows

Building on Windows is a matter of running `cmake` and `make`, as long as all dependencies are in your include path.

To create a standalone package, use the `windeployqt` tool included with your Qt installation. You will likely need to bundle some additional DLLs manually.

It is also possible to cross-compile using the `MinGW` compiler suite on Fedora and some other distributions.

### macOS

Run `cmake` and `make` as usual. To create a standalone package, use the `macdeployqt` tool included with your Qt installation.

## Publishing a release

The app discovers releases from the mirror directory
(`https://ftp2.osuosl.org/pub/acreetionos/`) using this chain, in order:

1. **`releases.json`** — the structured manifest. Preferred source: it carries the
   SHA-256 checksum and byte size for every edition, so downloads are verified
   after transfer and progress bars have real totals.
2. **`SHA256SUMS`** — the classic checksum sidecar. Used to verify downloads that
   were discovered from the directory listing instead of the manifest.
3. **The HTML directory listing** — auto-discovers any `Name-Version-Arch.iso`
   file, so new uploads appear without an app update.

To publish a new ISO:

```bash
# 1. Upload the ISO(s) to the mirror
# 2. Generate the manifest + checksum sidecar (from a local staging dir or the mirror URL)
python3 tools/generate-releases.py /path/to/staging --output dist/releases
# or: python3 tools/generate-releases.py https://ftp2.osuosl.org/pub/acreetionos/ --output dist/releases
# 3. Upload dist/releases/releases.json and dist/releases/SHA256SUMS next to the ISOs
# 4. Commit any releases.json change to this repo so offline users get it too
```

`generate-releases.py` computes the SHA-256 of every official edition ISO,
writes `releases.json` (embedded into the app as a fallback) and `SHA256SUMS`,
and prints exactly what to upload. Community/unknown ISOs are skipped by the
manifest but still auto-discovered by the app through the directory listing.

## Translation

If you want to help translate AcreetionOS Media Writer, please visit us atprojects/fedora-media-writer/mediawriter/).

Information about the individual AcreetionOS flavors is retrieved from the Fedora websites and translated as a separate project.

## Troubleshooting

If you experience any problem with the application, such as crashes or errors when writing to your drive, please open an issue here on GitHub.

Please attach the `AcreetionOSMediaWriter.log` file from your Documents folder (`$HOME/Documents` on Linux and macOS, `%USERPROFILE%\Documents` on Windows). It contains some non-sensitive information about your system and a log of all events during the session.

### My flash drive stopped working after writing

We understand how frustrating this can be, especially if it was a drive you relied on. We'd like to help explain what may have happened.

AcreetionOS Media Writer writes the image sequentially to your drive and then reads it back in full to verify the result — much like a large file copy, just at the raw device level. From the hardware's perspective, there is nothing unusual about this. There are no special commands involved that could instruct a drive to misbehave, and the application has no ability to damage the physical flash memory.

Flash drives do have a limited number of write cycles before the memory naturally wears out, but that limit is typically in the tens of thousands for standard hardware. A single write-and-verify pass uses a tiny fraction of that budget, so ordinary use of AcreetionOS Media Writer will not meaningfully shorten the life of a healthy drive.

If your drive is no longer recognized, here are the most likely explanations:

- **The drive is in an inconsistent state.** If the write process was interrupted, the drive's partition layout may be left in a state the operating system cannot recognize. This is recoverable — try using AcreetionOS Media Writer's own **Restore** feature, or use *Disk Management* on Windows / `fdisk` or `gparted` on Linux to reformat it manually.
- **The drive uses low-quality components.** Very cheap USB drives are often built with flash memory and controllers that cannot sustain the heat generated by continuous high-speed writing. AcreetionOS Media Writer writes at full speed and then immediately reads the entire drive back to verify — this sustained workload can push such drives beyond what they were designed to handle.
- **The drive was already near end of life.** Flash memory can fail suddenly once it reaches its limit. If the failure coincided with writing a Fedora image, it is very likely the drive would have failed just the same had you been copying any other large file at the time.

If the drive shows up as read-only or containing no media, the flash memory controller has most likely detected an internal failure. This is a sign that the drive has reached the end of its life and will need to be replaced.

For a more in-depth explanation of how flash drives fail and how to detect a faulty drive, the [Rufus FAQ](https://github.com/pbatard/rufus/wiki/FAQ) covers this topic in great detail.

## Security & Privacy

For details about cryptography, see [CRYPTOGRAPHY.md](CRYPTOGRAPHY.md).

For brief privacy information regarding User-Agent strings, see [PRIVACY.md](PRIVACY.md).
