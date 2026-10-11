#!/usr/bin/env python3
# fetch-misans.py <out dir>: downloads the MiSans UI fonts from Xiaomi and
# writes font1.ttf (MiSans Semibold) and font1-arabic.ttf (MiSans Arabic
# Semibold) into <out dir>.
#
# MiSans may ship inside the release (Xiaomi's FAQ: embedding is allowed when
# the software credits MiSans) but isn't committed to the repo, where each TTF
# would be a stand-alone copy. Xiaomi only publishes whole-family zips
# (MiSans.zip is ~228 MB), so the zips are read over HTTP Range requests and
# only the two entries are downloaded. Each file is checked against a pinned
# sha256; a font already in <out dir> with the right hash is kept (cache).
# Standard library only, so it runs on the host and on CI.
import hashlib
import os
import sys
import time
import urllib.error
import urllib.request
import zipfile

FONTS = [
    # (output name, zip url, entry, sha256 of the entry)
    ("font1.ttf",
     "https://hyperos.mi.com/font-download/MiSans.zip",
     "MiSans/ttf/MiSans-Semibold.ttf",
     "77c23f31ae124867778344970155a0c8d34a89897dedaab81aeee82ff00a4ce6"),
    ("font1-arabic.ttf",
     "https://hyperos.mi.com/font-download/MiSans_Arabic.zip",
     "MiSans Arabic/MiSans Arabic/ttf/MiSansArabic-Semibold.ttf",
     "8b7a899019b4517b4d222fe4afb61f43ca909f0c9022a22de64ef260076a1855"),
]


RETRIES = 4  # attempts per request; waits 2, 4, 8 s between them


def get(url, rng, timeout):
    """GET bytes <rng> of url, retrying server errors and network failures
    (CI runs this from many jobs at once and Xiaomi's CDN answers 504 at
    times). Returns (status, headers, body)."""
    req = urllib.request.Request(url, headers={"Range": f"bytes={rng}"})
    for attempt in range(1, RETRIES + 1):
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.status, r.headers, r.read()
        except urllib.error.HTTPError as e:
            if e.code < 500 and e.code != 429:
                raise
            err = e
        except (urllib.error.URLError, OSError) as e:
            err = e
        if attempt == RETRIES:
            raise err
        wait = 2 ** attempt
        print(f"{url}: {err}, retrying in {wait} s", file=sys.stderr)
        time.sleep(wait)


class RangeFile:
    """Seekable read-only file over HTTP Range requests, for zipfile."""

    def __init__(self, url):
        self.url = url
        self.pos = 0
        # HEAD has no Content-Length here; a 1-byte range reports the size
        _, headers, _ = get(url, "0-0", 60)
        self.size = int(headers["Content-Range"].rsplit("/", 1)[1])

    def seekable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, off, whence=0):
        if whence == 0:
            self.pos = off
        elif whence == 1:
            self.pos += off
        else:
            self.pos = self.size + off
        return self.pos

    def read(self, n=-1):
        if n is None or n < 0:
            n = self.size - self.pos
        n = min(n, self.size - self.pos)
        if n <= 0:
            return b""
        status, _, data = get(self.url, f"{self.pos}-{self.pos + n - 1}", 120)
        if status != 206:
            raise IOError(f"{self.url}: server ignored the Range request")
        self.pos += len(data)
        return data


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: fetch-misans.py <out dir>")
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, url, entry, want in FONTS:
        dest = os.path.join(out, name)
        if os.path.exists(dest) and sha256(dest) == want:
            print(f"{name}: cached")
            continue
        print(f"{name}: fetching {entry} from {url}")
        with zipfile.ZipFile(RangeFile(url)) as z:
            data = z.read(entry)
        got = hashlib.sha256(data).hexdigest()
        if got != want:
            sys.exit(f"{name}: sha256 {got} != pinned {want}")
        with open(dest + ".tmp", "wb") as f:
            f.write(data)
        os.replace(dest + ".tmp", dest)


if __name__ == "__main__":
    main()
