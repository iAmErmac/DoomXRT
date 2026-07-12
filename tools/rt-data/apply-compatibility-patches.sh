#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
rt_dir="${1:-rt}"

bash "$script_dir/apply-liquid-materials.sh" "$rt_dir"
bash "$script_dir/apply-heretic-lights.sh" "$rt_dir"
bash "$script_dir/apply-gldefs-lights.sh" "$rt_dir"

echo "Applied RT compatibility patches to $rt_dir"
