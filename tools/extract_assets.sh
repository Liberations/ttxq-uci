#!/usr/bin/env bash
# Extract libpikafishso.so and pikafish.nnue from an APK (the 天天象棋 / QQ Chess app).
# Usage: ./tools/extract_assets.sh path/to/ttxq.apk [output_dir]
set -euo pipefail

APK="${1:-}"
if [[ -z "$APK" || ! -f "$APK" ]]; then
  echo "usage: $0 <app.apk> [output_dir]" >&2
  exit 1
fi
OUT="${2:-vendor}"
mkdir -p "$OUT"

echo "Extracting from $APK ..."
# Python is used because it handles zip entries with arbitrary names/sizes reliably.
python3 - "$APK" "$OUT" <<'PY'
import sys, zipfile, os
apk, out = sys.argv[1], sys.argv[2]
wanted = {
    "lib/arm64-v8a/libpikafishso.so": "libpikafishso.so",
    "assets/pikafish.nnue": "pikafish.nnue",
}
with zipfile.ZipFile(apk) as z:
    names = set(z.namelist())
    for src, dst in wanted.items():
        if src not in names:
            print(f"  ! missing in APK: {src}")
            continue
        path = os.path.join(out, dst)
        with z.open(src) as r, open(path, "wb") as w:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                w.write(chunk)
        print(f"  + {dst}  ({os.path.getsize(path):,} bytes)")
PY
echo "Done."
