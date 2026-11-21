#!/bin/sh
# The release's PC downloads (platform/pc/RECOMPLAUNCHER.md): each launcher
# with recomplauncher.json beside it, at the archive's top, so that
# RecompLauncher can set the game up from a release:
#
#   build/release/N-Larged-XS-vX.Y.Z-Windows-x64.zip    NLargedXSLauncher.exe
#   build/release/N-Larged-XS-vX.Y.Z-Linux-x64.tar.gz   NLargedXSLauncher
#
# Run from the repository's root after build.sh (Windows) and
# build_linux.sh (Linux); either archive is left out if its launcher isn't
# built. recomplauncher.json's prepare.version is set to this version: a
# new release's game has to be made again from the ROM. Neither archive
# holds any ROM data (the launchers don't), and neither holds the made
# game.
set -e
VERSION=$(sed -n 's/^#define HOST_PORT_VERSION "\(.*\)"/\1/p' platform/pc/src/host_platform.h)
OUT=build/release
mkdir -p "$OUT"
python3 - "$VERSION" "$OUT" <<'EOF'
import io
import json
import os
import sys
import tarfile
import zipfile

version, out = sys.argv[1], sys.argv[2]
manifest = json.load(open("recomplauncher.json"))
manifest["prepare"]["version"] = version.lstrip("v")
data = (json.dumps(manifest, indent=2) + "\n").encode()

win = "build/launcher/NLargedXSLauncher.exe"
if os.path.exists(win):
    path = os.path.join(out, "N-Larged-XS-%s-Windows-x64.zip" % version)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(win, "NLargedXSLauncher.exe")
        z.writestr("recomplauncher.json", data)
    print(path)

lin = "build/launcher-linux/NLargedXSLauncher"
if os.path.exists(lin):
    path = os.path.join(out, "N-Larged-XS-%s-Linux-x64.tar.gz" % version)
    with tarfile.open(path, "w:gz") as t:
        info = t.gettarinfo(lin, "NLargedXSLauncher")
        info.mode = 0o755
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        with open(lin, "rb") as f:
            t.addfile(info, f)
        info = tarfile.TarInfo("recomplauncher.json")
        info.size = len(data)
        info.mode = 0o644
        t.addfile(info, io.BytesIO(data))
    print(path)
EOF
