#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../.." && pwd)"

usage() {
    cat <<'USAGE'
Usage: tools/rt-mods/install-mod.sh <mod-id> [recipe options]

Install a locally supplied external mod through a repo-owned RT compatibility
recipe. The mod asset itself stays outside git.

Known mod IDs:
  doom-voxels
  heretic-voxels

Examples:
  tools/rt-mods/install-mod.sh doom-voxels
  tools/rt-mods/install-mod.sh doom-voxels --install-kvx
  tools/rt-mods/install-mod.sh doom-voxels --uninstall
  tools/rt-mods/install-mod.sh heretic-voxels --variant full
  tools/rt-mods/install-mod.sh heretic-voxels --variant lite
  tools/rt-mods/install-mod.sh heretic-voxels --uninstall
USAGE
}

if [[ $# -lt 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

mod_id="$1"
shift

default_runtime_dir() {
    if [[ -n "${RT_DATA_DIR:-}" ]]; then
        printf '%s\n' "$RT_DATA_DIR"
    elif [[ -n "${GZDOOM_RT_RUNTIME_DIR:-}" ]]; then
        printf '%s\n' "$GZDOOM_RT_RUNTIME_DIR"
    elif [[ -n "${XDG_CACHE_HOME:-}" ]]; then
        printf '%s\n' "$XDG_CACHE_HOME/gzdoom-rt/runtime/current"
    else
        printf '%s\n' "$HOME/.cache/gzdoom-rt/runtime/current"
    fi
}

default_asset_dir() {
    if [[ -n "${GZDOOM_RT_ASSET_DIR:-}" ]]; then
        printf '%s\n' "$GZDOOM_RT_ASSET_DIR"
    elif [[ -d "$HOME/Games/gzdoom-rt/assets/gzdoom-rt-runtime/1.0.2/rt" ]]; then
        printf '%s\n' "$HOME/Games/gzdoom-rt/assets/gzdoom-rt-runtime/1.0.2/rt"
    elif [[ -d "$HOME/Games/gzdoom-rt/rt" ]]; then
        printf '%s\n' "$HOME/Games/gzdoom-rt/rt"
    elif [[ -d "$repo_root/rt" ]]; then
        printf '%s\n' "$repo_root/rt"
    fi
}

compat_rt_dir="$(default_runtime_dir)"
has_rt_dir=0
recipe_args=("$@")
for ((i = 0; i < ${#recipe_args[@]}; i++)); do
    case "${recipe_args[$i]}" in
        --rt-dir=*)
            compat_rt_dir="${recipe_args[$i]#--rt-dir=}"
            has_rt_dir=1
            ;;
        --rt-dir)
            if (( i + 1 < ${#recipe_args[@]} )); then
                compat_rt_dir="${recipe_args[$((i + 1))]}"
                has_rt_dir=1
            fi
            ;;
    esac
done

if [[ ! -f "$compat_rt_dir/data/textures.json" ]]; then
    asset_rt_dir="$(default_asset_dir)"
    if [[ -n "$asset_rt_dir" ]]; then
        "$repo_root/tools/rt-runtime/prepare-runtime.sh" "$asset_rt_dir" "$compat_rt_dir"
    fi
fi

if (( ! has_rt_dir )); then
    recipe_args+=(--rt-dir "$compat_rt_dir")
fi

case "$mod_id" in
    doom-voxels)
        python3 "$script_dir/recipes/doom_voxels.py" --repo-root "$repo_root" "${recipe_args[@]}"
        ;;
    heretic-voxels)
        python3 "$script_dir/recipes/heretic_voxels.py" --repo-root "$repo_root" "${recipe_args[@]}"
        ;;
    *)
        echo "Unknown RT mod recipe: $mod_id" >&2
        usage >&2
        exit 2
        ;;
esac

if [[ -f "$compat_rt_dir/data/textures.json" ]]; then
    "$repo_root/tools/rt-data/apply-compatibility-patches.sh" "$compat_rt_dir"
else
    echo "Skipped RT compatibility baseline: missing $compat_rt_dir/data/textures.json" >&2
fi
