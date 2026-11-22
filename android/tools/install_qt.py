#!/usr/bin/env python3
"""Install the pinned ARM64 Android Qt SDK using verified HTTPS archives."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

from install_qt_macos import download_archive, https_url, FALLBACK_MIRROR


def main():
    if len(sys.argv) != 3:
        raise ValueError("usage: android/tools/install_qt.py <https-mirror> <output-directory>")
    mirror = https_url(sys.argv[1]).rstrip("/") + "/"
    output = Path(sys.argv[2]).resolve()
    manifest = json.loads(Path(__file__).with_name("qt-sha256.json").read_text())
    cache = output.parent / "qt-downloads"
    cache.mkdir(parents=True, exist_ok=True)
    archives = [download_archive(path, digest, cache, [mirror, FALLBACK_MIRROR])
                for path, digest in manifest["archives"].items()]
    import py7zr
    from aqt.archives import TargetConfig
    from aqt.updater import Updater
    sdk = output / manifest["version"] / "android_arm64_v8a"
    sdk.mkdir(parents=True, exist_ok=True)
    for archive in archives:
        with py7zr.SevenZipFile(archive) as source:
            source.extractall(path=sdk)
    Updater.update(TargetConfig(manifest["version"], "android", "android_arm64_v8a", "mac"), output, None)
    print("Verified Qt Android SDK installed under " + str(sdk))


if __name__ == "__main__":
    main()
