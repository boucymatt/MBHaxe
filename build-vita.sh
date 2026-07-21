#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")" && pwd)"
workspace_dir="$(cd "${repo_dir}/.." && pwd)"
build_dir="${MBHAXE_VITA_BUILD_DIR:-${repo_dir}/vita/build}"
haxelib_repo="${workspace_dir}/.haxelib-repo"
hashlink_root="${workspace_dir}/deps/hashlink"

cd "${repo_dir}"
HAXELIB_PATH="${haxelib_repo}" haxelib dev hashlink \
	"${hashlink_root}/other/haxelib"
HAXELIB_PATH="${haxelib_repo}" haxe compile-vita.hxml
cmake -S vita -B "${build_dir}" \
	-DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DHASHLINK_ROOT="${hashlink_root}"
cmake --build "${build_dir}" --parallel

printf 'VPK: %s\n' "${build_dir}/mbhaxe-vita.vpk"
