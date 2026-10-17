#!/usr/bin/env python3
"""Install the pinned macOS Qt SDK without an online checksum service."""

import hashlib
import json
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request


MAXIMUM_ARCHIVE_BYTES = 200 * 1024 * 1024
FALLBACK_MIRROR = "https://mirror.netcologne.de/qtproject/"


def https_url(url):
    parsed = urllib.parse.urlsplit(url)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username is not None
            or parsed.password is not None or parsed.port not in (None, 443)
            or parsed.query or parsed.fragment):
        raise ValueError("Qt downloads require an HTTPS URL without credentials or custom ports")
    return url


class HttpsRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        https_url(new_url)
        return super().redirect_request(request, response, code, message, headers, new_url)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def download_archive(relative_path, expected_hash, cache, mirrors):
    archive = cache / Path(relative_path).name
    if archive.is_file() and sha256(archive) == expected_hash:
        return archive
    partial = archive.with_suffix(".part")
    opener = urllib.request.build_opener(HttpsRedirectHandler())
    for mirror in mirrors:
        url = https_url(mirror.rstrip("/") + "/" + relative_path)
        print("Downloading " + archive.name + " from " + urllib.parse.urlsplit(url).hostname, flush=True)
        try:
            with opener.open(url, timeout=30) as response, partial.open("wb") as target:
                size = 0
                while chunk := response.read(1024 * 1024):
                    size += len(chunk)
                    if size > MAXIMUM_ARCHIVE_BYTES:
                        raise ValueError("Qt archive exceeds the size limit")
                    target.write(chunk)
            if sha256(partial) != expected_hash:
                raise ValueError("Qt archive SHA-256 mismatch: " + archive.name)
            partial.replace(archive)
            return archive
        except (OSError, urllib.error.URLError) as error:
            print("Qt mirror download failed: " + str(error), file=sys.stderr)
        finally:
            partial.unlink(missing_ok=True)
    raise RuntimeError("No HTTPS mirror could supply " + archive.name)


def main():
    if len(sys.argv) != 3:
        raise ValueError("usage: install_qt_macos.py <https-mirror> <output-directory>")
    mirror = https_url(sys.argv[1]).rstrip("/") + "/"
    output = Path(sys.argv[2]).resolve()
    manifest = json.loads(Path(__file__).with_name("qt-macos-sha256.json").read_text())
    mirrors = list(dict.fromkeys([mirror, FALLBACK_MIRROR]))
    cache = output.parent / "qt-downloads"
    cache.mkdir(parents=True, exist_ok=True)
    # Verify every archive before extracting any of them.
    archives = [download_archive(path, digest, cache, mirrors)
                for path, digest in manifest["archives"].items()]

    import py7zr
    from aqt.archives import TargetConfig
    from aqt.updater import Updater

    sdk = output / manifest["version"] / "macos"
    sdk.mkdir(parents=True, exist_ok=True)
    for archive in archives:
        print("Extracting " + archive.name, flush=True)
        with py7zr.SevenZipFile(archive) as source:
            source.extractall(path=sdk)
    # Reuse relocation from the pinned aqtinstall version, as its CLI does.
    Updater.update(TargetConfig(manifest["version"], "desktop", "clang_64", "mac"), output, None)
    print("Verified Qt SDK installed under " + str(output / manifest["version"] / "macos"))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
