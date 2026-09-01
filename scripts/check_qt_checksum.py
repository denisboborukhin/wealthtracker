#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
import urllib.error
import urllib.parse
import urllib.request


MAXIMUM_RESPONSE_BYTES = 4_096


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_qt_checksum.py <https-url>", file=sys.stderr)
        return 2

    url = sys.argv[1]
    try:
        parsed = urllib.parse.urlsplit(url)
        port = parsed.port
    except ValueError:
        print("Qt checksum URL is invalid.", file=sys.stderr)
        return 2
    if (
        parsed.scheme != "https"
        or parsed.hostname != "download.qt.io"
        or parsed.username is not None
        or parsed.password is not None
        or port not in (None, 443)
    ):
        print("Qt checksum URL must use the official HTTPS origin.", file=sys.stderr)
        return 2

    request = urllib.request.Request(url, headers={"User-Agent": "WealthTracker-bootstrap/1"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            declared_size = response.headers.get("Content-Length")
            if declared_size is not None and int(declared_size) > MAXIMUM_RESPONSE_BYTES:
                raise ValueError("checksum response is oversized")
            body = response.read(MAXIMUM_RESPONSE_BYTES + 1)
    except (OSError, urllib.error.URLError, ValueError) as error:
        print(f"Qt checksum service is unavailable: {error}", file=sys.stderr)
        return 1

    if len(body) > MAXIMUM_RESPONSE_BYTES:
        print("Qt checksum response is oversized.", file=sys.stderr)
        return 1
    checksum = body.split(maxsplit=1)[0] if body else b""
    if re.fullmatch(rb"[0-9a-fA-F]{64}", checksum) is None:
        print("Qt checksum service returned an invalid SHA-256 value.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
