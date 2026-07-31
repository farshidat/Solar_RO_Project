# PlatformIO pre-script: gzip LittleFS web assets (*.gz siblings).
Import("env")  # noqa: F821 — provided by PlatformIO

from pathlib import Path
import gzip

ROOT = Path(env["PROJECT_DIR"]) / "data"  # noqa: F821
TARGETS = ("index.html", "style.css", "app.js", "report.js", "sw.js", "manifest.json", "icon.svg")

for name in TARGETS:
    src = ROOT / name
    if not src.is_file():
        continue
    dst = ROOT / (name + ".gz")
    data = src.read_bytes()
    with gzip.open(dst, "wb", compresslevel=9) as f:
        f.write(data)
    print(f"[gzip] {src.name} -> {dst.name} ({len(data)} -> {dst.stat().st_size})")
