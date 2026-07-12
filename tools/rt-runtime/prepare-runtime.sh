#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
compat_version="compat-9"

usage() {
    cat <<'USAGE'
Usage: tools/rt-runtime/prepare-runtime.sh <source-rt-dir> [target-runtime-dir]

Compose a generated GZDoom RT runtime directory from an immutable RT asset
source. Large asset directories are symlinked, mutable metadata is copied and
patched, and the source directory is left untouched.

Defaults:
  target-runtime-dir: $GZDOOM_RT_RUNTIME_DIR, or
                      $XDG_CACHE_HOME/gzdoom-rt/runtime/current, or
                      ~/.cache/gzdoom-rt/runtime/current
USAGE
}

if [[ $# -lt 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

source_rt="$1"
target_arg="${2:-${GZDOOM_RT_RUNTIME_DIR:-}}"

if [[ -z "$target_arg" ]]; then
    if [[ -n "${XDG_CACHE_HOME:-}" ]]; then
        target_arg="$XDG_CACHE_HOME/gzdoom-rt/runtime/current"
    else
        target_arg="$HOME/.cache/gzdoom-rt/runtime/current"
    fi
fi

source_rt="$(realpath -m "$source_rt")"
target_parent="$(dirname "$target_arg")"
target_name="$(basename "$target_arg")"
target_arg="$(realpath -m "$target_parent")/$target_name"

require_path() {
    local path="$1"
    if [[ ! -e "$path" ]]; then
        echo "Missing required RT asset path: $path" >&2
        exit 1
    fi
}

require_path "$source_rt/data/textures.json"
require_path "$source_rt/wad"
require_path "$source_rt/mat"
require_path "$source_rt/shaders"
require_path "$source_rt/BlueNoise_LDR_RGBA_128.ktx2"
require_path "$source_rt/WaterNormal_n.ktx2"

if [[ "$source_rt" == "$target_arg" ]]; then
    echo "Source and target runtime are the same path: $source_rt" >&2
    exit 1
fi

manifest_key="$source_rt|$compat_version"
fingerprint="$(printf '%s' "$manifest_key" | sha256sum | cut -c1-16)"

if [[ "$(basename "$target_arg")" == "current" ]]; then
    runtime_parent="$(dirname "$target_arg")"
    runtime_dir="$runtime_parent/$fingerprint"
    current_link="$target_arg"
else
    runtime_parent="$(dirname "$target_arg")"
    runtime_dir="$target_arg"
    current_link=""
fi

tmp_dir="$runtime_dir.tmp.$$"
rm -rf "$tmp_dir"
mkdir -p "$tmp_dir"

link_entry() {
    local name="$1"
    if [[ -e "$source_rt/$name" ]]; then
        ln -s "$source_rt/$name" "$tmp_dir/$name"
    fi
}

for name in \
    bin \
    bin_remix \
    launcher \
    mat_src \
    replace \
    scenes \
    shaders \
    wad \
    BlueNoise_LDR_RGBA_128.ktx2 \
    DirtMask.ktx2 \
    SceneBuildWarning.ktx2 \
    WaterNormal_n.ktx2 \
    CreateKTX2.py \
    LICENSE \
    RTGL1.json \
    RTGL1.json-example \
    RTGL1_Remix.json-example
do
    link_entry "$name"
done

mkdir -p "$tmp_dir/mat"
find "$source_rt/mat" -mindepth 1 -maxdepth 1 -print0 |
    while IFS= read -r -d '' item; do
        ln -s "$item" "$tmp_dir/mat/$(basename "$item")"
    done

cp -a "$source_rt/data" "$tmp_dir/data"
mkdir -p "$tmp_dir/autoload"

patch_static_scenes() {
    local scenes_src="$source_rt/scenes"
    local scenes_dst="$tmp_dir/scenes"

    if [[ ! -d "$scenes_src" ]]; then
        return 0
    fi

    if [[ -L "$scenes_dst" ]]; then
        rm "$scenes_dst"
        mkdir -p "$scenes_dst"
        find "$scenes_src" -mindepth 1 -maxdepth 1 -print0 |
            while IFS= read -r -d '' item; do
                ln -s "$item" "$scenes_dst/$(basename "$item")"
            done
    fi

    find "$scenes_dst" -mindepth 1 -maxdepth 1 -print0 |
        while IFS= read -r -d '' scene_dst; do
            local scene_name
            local scene_src
            local gltf

            scene_name="$(basename "$scene_dst")"
            scene_src="$scenes_src/$scene_name"
            gltf="$scene_dst/$scene_name.gltf"

            case "$scene_name" in
                heretic_wad_*) ;;
                *) continue ;;
            esac

            if [[ ! -f "$gltf" ]] ||
                ! grep -Eq 'mat_junction/LAVA[1-4]\.tga' "$gltf"; then
                continue
            fi

            if [[ -L "$scene_dst" ]]; then
                rm "$scene_dst"
                cp -a "$scene_src" "$scene_dst"
                gltf="$scene_dst/$scene_name.gltf"
            fi

            python3 - "$gltf" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as fh:
    data = json.load(fh)

images = data.get("images", [])
textures = data.get("textures", [])
lavafall_uris = {f"mat_junction/LAVA{i}.tga" for i in range(1, 5)}

for material_index, material in enumerate(data.get("materials", [])):
    pbr = material.setdefault("pbrMetallicRoughness", {})
    base = pbr.get("baseColorTexture", {})
    texture_index = base.get("index")
    if texture_index is None or texture_index >= len(textures):
        continue
    source_index = textures[texture_index].get("source")
    if source_index is None or source_index >= len(images):
        continue
    uri = images[source_index].get("uri", "")
    if uri in lavafall_uris:
        material.pop("emissiveFactor", None)
        material.pop("emissiveTexture", None)
        pbr["metallicFactor"] = 0
        pbr["roughnessFactor"] = 1.0

with open(path, "w", encoding="utf-8") as fh:
    json.dump(data, fh, indent=2)
    fh.write("\n")
PY
        done
}

patch_static_scenes

bash "$repo_root/tools/rt-data/apply-compatibility-patches.sh" "$tmp_dir"

cat > "$tmp_dir/.gzdoom-rt-runtime" <<EOF
source=$source_rt
compat=$compat_version
fingerprint=$fingerprint
prepared_by=tools/rt-runtime/prepare-runtime.sh
EOF

mkdir -p "$runtime_parent"
rm -rf "$runtime_dir"
mv "$tmp_dir" "$runtime_dir"

if [[ -n "$current_link" ]]; then
    ln -sfn "$runtime_dir" "$current_link"
fi

echo "Prepared RT runtime: $runtime_dir"
if [[ -n "$current_link" ]]; then
    echo "Updated current runtime: $current_link -> $runtime_dir"
fi
