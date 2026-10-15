"""Validate a built channel manifest before a maintainer opens its activation PR."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server"))
from update import API_VERSION, SHA, VERSION, _allowed_release

def publish(source: Path, destination: Path) -> None:
    data = json.loads(source.read_text(encoding="utf-8-sig"))
    if not VERSION.fullmatch(data.get("version", "")) or data.get("apiVersion") != API_VERSION:
        raise ValueError("Incompatible release version")
    if not SHA.fullmatch(data.get("sha256", "")) or not _allowed_release(data.get("url", ""), False):
        raise ValueError("Untrusted release URL or invalid hash")
    if f"/framefit-v{data['version']}/framefit-{data['version']}-windows-x64.zip" not in data["url"]:
        raise ValueError("Release URL version does not match manifest version")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--destination", type=Path, default=Path(__file__).resolve().parents[1] / "channels" / "stable.json")
    args = parser.parse_args()
    publish(args.manifest, args.destination)
