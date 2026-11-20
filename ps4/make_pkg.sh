#!/usr/bin/env bash
# Build the installable PS4 .pkg from eboot.bin and the port's assets/ folder (fonts, menus, locales: no game data).
# The ROM is supplied by the user in /data/jfg on the console.
# Usage: ps4/make_pkg.sh <eboot.bin> <output dir>
set -euo pipefail

EBOOT="$1"
mkdir -p "$2"
OUTDIR="$(cd "$2" && pwd)"

PS4_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$PS4_DIR/.." && pwd)"
OO_PS4_TOOLCHAIN="${OO_PS4_TOOLCHAIN:-$HOME/ps4dev/OpenOrbis/PS4Toolchain}"
export OO_PS4_TOOLCHAIN
TOOLS="$OO_PS4_TOOLCHAIN/bin/linux"

TITLE="Jet Force Gemini"
VERSION="${JFG_PKG_VERSION:-01.13}"
TITLE_ID="JFGR00001"
CONTENT_ID="IV0000-JFGR00001_00-JFGRECOMPPS40000"

export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
export LD_LIBRARY_PATH="${PS4_HOSTLIBS:-$HOME/ps4dev/hostlibs/usr/lib}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

STAGE="$OUTDIR/pkg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/sce_sys/about" "$STAGE/sce_module"

cp "$PS4_DIR/sce_sys/icon0.png" "$STAGE/sce_sys/"
cp "$PS4_DIR/sce_sys/about/right.sprx" "$STAGE/sce_sys/about/"
cp "$PS4_DIR/sce_module/libc.prx" "$PS4_DIR/sce_module/libSceFios2.prx" "$STAGE/sce_module/"
cp "$EBOOT" "$STAGE/eboot.bin"
cp -r "$ROOT/assets" "$STAGE/assets"
DATA_TREES="assets"

SFO="$STAGE/sce_sys/param.sfo"
"$TOOLS/PkgTool.Core" sfo_new "$SFO"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" APP_TYPE  --type Integer --maxsize 4   --value 1
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" APP_VER   --type Utf8    --maxsize 8   --value "$VERSION"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" ATTRIBUTE --type Integer --maxsize 4   --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" CATEGORY  --type Utf8    --maxsize 4   --value gd
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" CONTENT_ID --type Utf8   --maxsize 48  --value "$CONTENT_ID"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" SYSTEM_VER --type Integer --maxsize 4  --value 0
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" TITLE     --type Utf8    --maxsize 128 --value "$TITLE"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" TITLE_ID  --type Utf8    --maxsize 12  --value "$TITLE_ID"
"$TOOLS/PkgTool.Core" sfo_setentry "$SFO" VERSION   --type Utf8    --maxsize 8   --value "$VERSION"

cd "$STAGE"
FILES="eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png"
FILES="$FILES sce_module/libc.prx sce_module/libSceFios2.prx"
while IFS= read -r f; do
    case "$f" in
        *" "*) echo "error: file name with a space: $f" >&2; exit 1 ;;
    esac
    FILES="$FILES ${f#./}"
done < <(for tree in $DATA_TREES; do find "./$tree" -type f; done | sort)

"$TOOLS/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$FILES"

# create-gp4 declares only a fixed set of folders; replace whatever it wrote for the data trees with their full
# directory tree so PkgTool can place every file.
python3 - pkg.gp4 $DATA_TREES <<'PYEOF'
import os, re, sys
gp4, tops = sys.argv[1], sys.argv[2:]

def tree(path, depth):
    pad = "\t" * depth
    children = sorted(d for d in os.listdir(path) if os.path.isdir(os.path.join(path, d)))
    name = os.path.basename(path)
    if not children:
        return f'{pad}<dir targ_name="{name}" />\n'
    inner = "".join(tree(os.path.join(path, c), depth + 1) for c in children)
    return f'{pad}<dir targ_name="{name}">\n{inner}{pad}</dir>\n'

text = open(gp4, encoding="utf-8").read()
assert "</rootdir>" in text
for t in tops:
    # drop an existing declaration of this top folder (self-closing or with children)
    text = re.sub(r'\t\t<dir targ_name="%s"\s*/>\n' % re.escape(t), "", text)
    text = re.sub(r'\t\t<dir targ_name="%s">.*?\n\t\t</dir>\n' % re.escape(t), "", text, flags=re.S)
text = text.replace("\t</rootdir>", "".join(tree(t, 2) for t in tops) + "\t</rootdir>", 1)
open(gp4, "w", encoding="utf-8").write(text)
PYEOF

"$TOOLS/PkgTool.Core" pkg_build pkg.gp4 "$OUTDIR"

cp -f "$OUTDIR/$CONTENT_ID.pkg" "$OUTDIR/JetForceGemini-PS4-$VERSION.pkg"
echo "OK: $OUTDIR/JetForceGemini-PS4-$VERSION.pkg"
