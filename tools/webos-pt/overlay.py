#!/usr/bin/env python3
"""Reproducible pt-BR layer over the pinned nucleo-webos build.

Apply after a webos sync. Exact upstream hashes prevent overwriting a changed
upstream file; rebase its patch before syncing a different webos version.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def digest(data):
    return hashlib.sha256(data.replace(b"\r\n", b"\n") if b"\0" not in data[:8000] else data).hexdigest()


def inventory():
    manifest = json.loads((HERE / "manifest.json").read_text())
    files = sorted(p for p in HERE.rglob("*") if p.is_file() and p.suffix in {".json", ".patch", ".py"})
    stamp = hashlib.sha256()
    for p in files:
        stamp.update(str(p.relative_to(HERE)).encode() + b"\0" + p.read_bytes())
    return manifest, stamp.hexdigest()


def apply(check=False, update_lock=True):
    manifest, stamp = inventory()
    changed = []
    # Check every base before making any change, including a patch that is
    # already materialized. Never quietly overwrite another translation.
    for row in manifest["patches"]:
        p = ROOT / row["path"]
        if not p.exists() or digest(p.read_bytes()) not in {row["before"], row["after"]}:
            raise RuntimeError(f"Upstream changed: {row['path']}; rebase tools/webos-pt/patches first")
        if check and digest(p.read_bytes()) != row["after"]:
            raise RuntimeError(f"pt-BR patch not applied: {row['path']}")
    for row in manifest["patches"]:
        p = ROOT / row["path"]
        if not check and digest(p.read_bytes()) != row["after"]:
            subprocess.run(["git", "apply", str(HERE / row["patch"])], cwd=ROOT, check=True)
        changed.append(p)
    for src in sorted((HERE / "catalogs").rglob("*.json")):
        dest = ROOT / src.relative_to(HERE / "catalogs")
        if check:
            if not dest.exists() or dest.read_bytes() != src.read_bytes():
                raise RuntimeError(f"Stale pt-BR catalog: {dest.relative_to(ROOT)}")
        else:
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(src.read_bytes())
        changed.append(dest)
    # The native cascade is rebuilt independently from the shared web shell.
    # Its normal parity gate checks this artifact's embedded source fingerprint.
    for name in ["anima-local.mjs", "anima-local.wasm"]:
        src = ROOT / "build" / "anima-local" / name
        dest = ROOT / "apps/anima/www/local" / name
        if not check and src.exists():
            dest.write_bytes(src.read_bytes())
        if dest.exists():
            changed.append(dest)
    lock_path = ROOT / "webos.lock"
    lock = json.loads(lock_path.read_text())
    if check:
        if lock.get("pt_overlay", {}).get("sha256") != stamp:
            raise RuntimeError("pt-BR overlay changed; run python3 tools/webos-pt/overlay.py --apply")
        for p in changed:
            twin = p.with_name(p.name + ".gz")
            if not twin.exists() or digest(gzip.decompress(twin.read_bytes())) != digest(p.read_bytes()):
                raise RuntimeError(f"Stale gzip: {twin.relative_to(ROOT)}")
    else:
        lock["_doc"] = "Written by tools/webos-sync.py with the reproducible tools/webos-pt layer; do not hand-edit generated web assets."
        for p in changed:
            p.with_name(p.name + ".gz").write_bytes(gzip.compress(p.read_bytes(), compresslevel=9, mtime=0))
            lock["files"][str(p.relative_to(ROOT))] = digest(p.read_bytes())
        lock["pt_overlay"] = {"version": 1, "sha256": stamp}
        if update_lock:
            lock_path.write_text(json.dumps(lock, ensure_ascii=False, indent=1, sort_keys=True) + "\n")
    return [str(p.relative_to(ROOT)) for p in changed], {"version": 1, "sha256": stamp}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        paths, _ = apply(check=not args.apply)
        print(f"pt-BR web overlay: {len(paths)} files {'applied' if args.apply else 'current'}")
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + "\n")
