set -euo pipefail
OUT=$PWD
cmake -S lib/N64Recomp -B build-tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-tools --parallel --target N64RecompCLI RSPRecomp
python3 tools/prepare_overlays.py baserom.us.z64 - baserom.us.recomp.z64 - tools/jfg.us.dynamic_relocs.json
rm -rf RecompiledFuncs && mkdir RecompiledFuncs
python3 tools/refuse_mod_hooks.py RecompiledFuncs tools/jfg.us.dynamic_relocs.json JFGRecompSyms/jfg.us.syms.toml jfg.us.toml
build-tools/N64Recomp jfg.us.toml > recomp.log 2>&1 || { tail -20 recomp.log; exit 1; }
build-tools/RSPRecomp rsp-aspMain.jfg.toml
python3 tools/merge_gen_anim_data.py RecompiledFuncs
python3 tools/fix_dynamic_relocs.py RecompiledFuncs tools/jfg.us.dynamic_relocs.json baserom.us.recomp.z64 JFGRecompSyms/jfg.us.syms.toml
echo GEN_OK; ls RecompiledFuncs | wc -l; du -sh RecompiledFuncs
