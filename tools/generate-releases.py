#!/usr/bin/env python3
"""
AcreetionOS Media Writer - release manifest generator

Regenerates the release metadata that the Media Writer app consumes:

  releases.json   - structured manifest with sha256 + size (preferred source)
  SHA256SUMS      - classic checksum sidecar (used when the listing is parsed)

Usage:
  python3 tools/generate-releases.py <dir-or-url> [--output DIR]

  <dir-or-url>    local directory containing the ISOs, or an HTTP(S) URL to an
                  Apache-style directory listing (the app's releasesDir).
  --output DIR    where to write releases.json + SHA256SUMS (default: current dir)

The published pipeline is:
  1. Upload new ISO(s) to the mirror (ftp2.osuosl.org/pub/acreetionos/)
  2. Run this script against the mirror (or the local staging dir)
  3. Upload the generated releases.json + SHA256SUMS next to the ISOs
  4. The app picks everything up automatically, with full integrity checks

Only official editions (subvariant match) get a manifest entry; any other ISO
in the directory is still auto-discovered by the app via the listing fallback.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

# Editions that are listed in the app's metadata.json (src/app/data/assets/metadata.json).
# Keep in sync when editions are added or renamed.
KNOWN_EDITIONS = {
    "acreetionos": "AcreetionOS",
    "acreetionos_xl": "AcreetionOS_XL",
}

# Filenames like AcreetionOS-1.0-x86_64.iso or AcreetionOS_XL-1.0-x86_64.iso
ISO_RE = re.compile(r"^(.+?)[-_](\d+\.\d+)-([\w_]+)\.iso$")


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def http_get(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "AcreetionOSMediaWriter/5.4.1 (manifest-generator)"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def discover(source: str):
    """Return {filename: (size_bytes, mtime_iso)} for every ISO at source."""
    if source.startswith(("http://", "https://")):
        html = http_get(source).decode("utf-8", "replace")
        # Apache row: <a href="Name.iso">Name.iso</a></td><td>date</td><td>size</td>
        re_row = re.compile(r'<a\s+href="([^"]+\.iso)"[^>]*>.*?</a>\s*</td>\s*<td[^>]*>\s*([^<]*?)\s*</td>\s*<td[^>]*>\s*([^<]*?)\s*</td>')
        found = {}
        for m in re_row.finditer(html):
            name = m.group(1)
            if "/" in name or name.startswith("?"):
                continue
            size = parse_apache_size(m.group(3))
            mtime = m.group(2).strip()
            found[Path(name).name] = (size, mtime)
        return found

    root = Path(source)
    found = {}
    for p in sorted(root.glob("*.iso")):
        st = p.stat()
        mtime = datetime.fromtimestamp(st.st_mtime, tz=timezone.utc).strftime("%Y-%m-%d %H:%M")
        found[p.name] = (st.st_size, mtime)
    return found


def parse_apache_size(cell: str) -> int:
    cell = cell.strip()
    if not cell:
        return 0
    m = re.match(r"^([\d.]+)\s*([KMG]?)$", cell)
    if not m:
        try:
            return int(cell)
        except ValueError:
            return 0
    v = float(m.group(1))
    unit = m.group(2)
    if unit == "K":
        v *= 1024
    elif unit == "M":
        v *= 1024 * 1024
    elif unit == "G":
        v *= 1024 * 1024 * 1024
    return int(v)


def fetch_iso(source: str, name: str) -> bytes:
    """Fetch or read the ISO, streaming to disk for hashing when remote."""
    if source.startswith(("http://", "https://")):
        return http_get(source.rstrip("/") + "/" + name)
    return (Path(source) / name).read_bytes()


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate releases.json + SHA256SUMS for AcreetionOS Media Writer")
    ap.add_argument("source", help="local dir with ISOs or URL of the directory listing")
    ap.add_argument("--output", default=".", help="output directory (default: current dir)")
    ap.add_argument("--mirror", default="https://ftp2.osuosl.org/pub/acreetionos/",
                    help="public mirror base URL written into releases.json (default: OSUOSL)")
    args = ap.parse_args()

    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)

    isos = discover(args.source)
    if not isos:
        print("No ISO files found at", args.source, file=sys.stderr)
        return 1

    manifest = []
    sums_lines = []
    for name, (size, mtime) in isos.items():
        m = ISO_RE.match(name)
        if not m:
            print("Skipping (unrecognized filename pattern):", name)
            continue
        subvariant_key = m.group(1).lower()
        if subvariant_key not in KNOWN_EDITIONS:
            print(f"Skipping (community/unknown edition): {name}")
            continue
        # Normalize to the major version number, matching the app's convention
        version = m.group(2).split(".")[0]

        print(f"Hashing {name} ({size / 1e9:.2f} GB)...")
        digest = hashlib.sha256()
        exact_size = size
        if args.source.startswith(("http://", "https://")):
            # Stream remote download through the hash to avoid holding GBs in RAM;
            # use the exact Content-Length rather than the listing's rounded size
            req = urllib.request.Request(args.source.rstrip("/") + "/" + name,
                                         headers={"User-Agent": "AcreetionOSMediaWriter (manifest-generator)"})
            with urllib.request.urlopen(req, timeout=3600) as resp:
                if resp.headers.get("Content-Length"):
                    exact_size = int(resp.headers["Content-Length"])
                while chunk := resp.read(1024 * 1024):
                    digest.update(chunk)
        else:
            with (Path(args.source) / name).open("rb") as f:
                while chunk := f.read(1024 * 256):
                    digest.update(chunk)
        sha = digest.hexdigest()

        release_date = mtime[:10] if mtime else datetime.now(timezone.utc).strftime("%Y-%m-%d")
        manifest.append({
            "arch": m.group(3),
            "link": args.mirror.rstrip("/") + "/" + name,
            "variant": "product",
            "subvariant": KNOWN_EDITIONS[subvariant_key],
            "version": version,
            "sha256": sha,
            "size": str(exact_size),
            "releaseDate": release_date,
        })
        sums_lines.append(f"{sha}  {name}")

    manifest.sort(key=lambda e: e["subvariant"])
    (out / "releases.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (out / "SHA256SUMS").write_text("".join(f"{l}\n" for l in sums_lines))

    print(f"\nWrote {out / 'releases.json'} ({len(manifest)} editions)")
    print(f"Wrote {out / 'SHA256SUMS'} ({len(sums_lines)} entries)")
    print("Upload both files next to the ISOs on the mirror, then commit any"
          " releases.json change to the app repo so offline users get it too.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
