#!/usr/bin/env bash
# Jet Force Gemini PS4: compile, link, package. Stops at the first error. Run from the repo root in WSL.
#   bash ps4/build.sh            (JFG_PKG_VERSION=01.04 bash ps4/build.sh to set the version)
# Needs the game code generated from your ROM first: bash ps4/gen.sh (with baserom.us.z64 in the repo root).
set -euo pipefail
cd "$(dirname "$0")/.."

# The PS4 changes to N64ModernRuntime (a submodule of the base project) live in ps4/patches; apply them once.
PATCH="$PWD/ps4/patches/n64modernruntime.patch"
if git -C lib/N64ModernRuntime apply --reverse --check "$PATCH" 2>/dev/null; then
    : # already applied
else
    git -C lib/N64ModernRuntime apply "$PATCH"
    echo "[jfg-ps4] applied ps4/patches/n64modernruntime.patch"
fi

[ -f rsp/aspMain.cpp ] && ls RecompiledFuncs/*.c >/dev/null 2>&1 || {
    echo "[jfg-ps4] no generated game code: run bash ps4/gen.sh first (needs baserom.us.z64)" >&2
    exit 1
}

make -f ps4/Makefile -j"$(nproc)"
make -f ps4/Makefile eboot
bash ps4/make_pkg.sh build-ps4/eboot.bin build-ps4
