#!/usr/bin/env python3
"""Bundle published release firmware into the Pages site.

Runs during the GitHub Pages deploy. GitHub serves release assets from
``release-assets.githubusercontent.com`` without CORS headers, so the browser
cannot ``fetch()`` them directly. Downloading them here and serving them from
the same origin as the site removes that problem (and the API rate limit).

Writes ``public/firmware/<tag>.bin`` plus ``public/firmware/releases.json``.
"""

from __future__ import annotations

import json
import os
import urllib.request

REPO = os.environ["GITHUB_REPOSITORY"]
TOKEN = os.environ.get("GH_TOKEN", "")
OUT_DIR = os.path.join("public", "firmware")
MANIFEST = os.path.join(OUT_DIR, "releases.json")


def _headers(accept: str) -> dict[str, str]:
    headers = {"Accept": accept}
    if TOKEN:
        headers["Authorization"] = f"Bearer {TOKEN}"
    return headers


def api(path: str) -> list:
    request = urllib.request.Request(
        f"https://api.github.com/repos/{REPO}/{path}",
        headers=_headers("application/vnd.github+json"),
    )
    with urllib.request.urlopen(request) as response:
        return json.load(response)


def download(asset_url: str, dest: str) -> None:
    request = urllib.request.Request(asset_url, headers=_headers("application/octet-stream"))
    with urllib.request.urlopen(request) as response, open(dest, "wb") as out:
        while chunk := response.read(65536):
            out.write(chunk)


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)

    manifest = []
    for release in api("releases?per_page=100"):
        if release.get("draft"):
            continue
        assets = [a for a in release.get("assets", []) if a["name"].lower().endswith(".bin")]
        asset = next((a for a in assets if a["name"] == "firmware.bin"), assets[0] if assets else None)
        if asset is None:
            continue

        tag = release["tag_name"]
        filename = f"{tag}.bin"
        download(asset["url"], os.path.join(OUT_DIR, filename))
        manifest.append(
            {
                "tag": tag,
                "name": release.get("name") or tag,
                "publishedAt": release.get("published_at"),
                "size": asset["size"],
                "file": f"firmware/{filename}",
            }
        )
        print(f"bundled {tag}: {asset['name']} -> firmware/{filename}")

    with open(MANIFEST, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")
    print(f"wrote {MANIFEST} ({len(manifest)} release(s))")


if __name__ == "__main__":
    main()
