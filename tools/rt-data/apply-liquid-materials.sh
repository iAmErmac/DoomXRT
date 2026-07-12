#!/usr/bin/env bash
set -euo pipefail

rt_dir="${1:-rt}"
textures="$rt_dir/data/textures.json"
mat_dir="$rt_dir/mat"

if [[ ! -f "$textures" ]]; then
    echo "Missing $textures" >&2
    echo "Usage: bash tools/rt-data/apply-liquid-materials.sh /path/to/rt" >&2
    exit 1
fi

if [[ ! -d "$mat_dir" ]]; then
    echo "Missing $mat_dir" >&2
    exit 1
fi

backup="$textures.before-liquid-materials"
if [[ ! -f "$backup" ]]; then
    cp "$textures" "$backup"
fi

ensure_entry_after() {
    local name="$1"
    local after="$2"
    local line="$3"
    local tmp

    if grep -q "\"textureName\":\"$name\"" "$textures"; then
        return 0
    fi

    tmp="$(mktemp)"
    awk -v after="\"textureName\":\"$after\"" -v line="$line" '
        {
            print
            if (!inserted && index($0, after)) {
                print line
                inserted = 1
            }
        }
        END {
            if (!inserted) {
                exit 2
            }
        }
    ' "$textures" > "$tmp" || {
        rm -f "$tmp"
        echo "Could not find insertion anchor $after in $textures" >&2
        exit 1
    }
    mv "$tmp" "$textures"
}

mirror_line() {
    printf '    ,   { "textureName":"%s" ,"isMirror":true    ,"metallicDefault":1.0  ,"roughnessDefault":0.0 }' "$1"
}

lava_line() {
    printf '    ,   { "textureName":"%s"    ,"emissiveMult":0.45    ,"isWater":true     ,"metallicDefault":0.0  ,"roughnessDefault":0.25 ,"lightIntensity":600  ,"lightColorHEX":"ff4d20" ,"lightEvenOnDynamic":true }' "$1"
}

lava_floor_textures=(
    FLTLAVA1
    FLTLAVA2
    FLTLAVA3
    FLTLAVA4
    FLATHUH1
    X_001
    X_002
    X_003
    X_004
)

replace_entry() {
    local name="$1"
    local line="$2"
    local tmp

    if ! grep -q "\"textureName\":\"$name\"" "$textures"; then
        return 0
    fi

    tmp="$(mktemp)"
    awk -v name="\"textureName\":\"$name\"" -v line="$line" '
        index($0, name) {
            print line
            next
        }
        { print }
    ' "$textures" > "$tmp"
    mv "$tmp" "$textures"
}

# Doom uses LAVA1-LAVA4 as flats, but Heretic uses LAVA1 as a wall texture for
# lavafalls. Keep the shared bare names non-liquid and non-emissive; explicit
# floor lava aliases and patched static-scene floor materials carry the glow.
perl -pi -e 's/\{ "textureName":"LAVA([1234])"[^}]*\}/{ "textureName":"LAVA$1"    ,"emissiveMult":0.0     }/' "$textures"

ensure_entry_after "SWATER1"  "FWATER4"  "$(mirror_line "SWATER1")"
ensure_entry_after "SWATER2"  "SWATER1"  "$(mirror_line "SWATER2")"
ensure_entry_after "SWATER3"  "SWATER2"  "$(mirror_line "SWATER3")"
ensure_entry_after "SWATER4"  "SWATER3"  "$(mirror_line "SWATER4")"
ensure_entry_after "FLTWAWA1" "SWATER4"  "$(mirror_line "FLTWAWA1")"
ensure_entry_after "FLTWAWA2" "FLTWAWA1" "$(mirror_line "FLTWAWA2")"
ensure_entry_after "FLTWAWA3" "FLTWAWA2" "$(mirror_line "FLTWAWA3")"
ensure_entry_after "FLTFLWW1" "FLTWAWA3" "$(mirror_line "FLTFLWW1")"
ensure_entry_after "FLTFLWW2" "FLTFLWW1" "$(mirror_line "FLTFLWW2")"
ensure_entry_after "FLTFLWW3" "FLTFLWW2" "$(mirror_line "FLTFLWW3")"
ensure_entry_after "FLTSLUD1" "FLTFLWW3" "$(mirror_line "FLTSLUD1")"
ensure_entry_after "FLTSLUD2" "FLTSLUD1" "$(mirror_line "FLTSLUD2")"
ensure_entry_after "FLTSLUD3" "FLTSLUD2" "$(mirror_line "FLTSLUD3")"
ensure_entry_after "X_005"    "FLTSLUD3" "$(mirror_line "X_005")"
ensure_entry_after "X_009"    "X_005"    "$(mirror_line "X_009")"
ensure_entry_after "F_WATR01" "X_009"    "$(mirror_line "F_WATR01")"
ensure_entry_after "F_WATR02" "F_WATR01" "$(mirror_line "F_WATR02")"
ensure_entry_after "F_WATR03" "F_WATR02" "$(mirror_line "F_WATR03")"
ensure_entry_after "F_HWATR1" "F_WATR03" "$(mirror_line "F_HWATR1")"
ensure_entry_after "F_HWATR2" "F_HWATR1" "$(mirror_line "F_HWATR2")"
ensure_entry_after "F_HWATR3" "F_HWATR2" "$(mirror_line "F_HWATR3")"
ensure_entry_after "F_PWATR1" "F_HWATR3" "$(mirror_line "F_PWATR1")"
ensure_entry_after "F_PWATR2" "F_PWATR1" "$(mirror_line "F_PWATR2")"
ensure_entry_after "F_PWATR3" "F_PWATR2" "$(mirror_line "F_PWATR3")"
ensure_entry_after "P_VWATR1" "F_PWATR3" "$(mirror_line "P_VWATR1")"
ensure_entry_after "F_VWATR2" "P_VWATR1" "$(mirror_line "F_VWATR2")"
ensure_entry_after "F_VWATR3" "F_VWATR2" "$(mirror_line "F_VWATR3")"
ensure_entry_after "SLIME09"  "SLIME08"  "$(mirror_line "SLIME09")"
ensure_entry_after "SLIME10"  "SLIME09"  "$(mirror_line "SLIME10")"
ensure_entry_after "SLIME11"  "SLIME10"  "$(mirror_line "SLIME11")"
ensure_entry_after "SLIME12"  "SLIME11"  "$(mirror_line "SLIME12")"
ensure_entry_after "SLIME13"  "SLIME12"  "$(mirror_line "SLIME13")"
ensure_entry_after "SLIME14"  "SLIME13"  "$(mirror_line "SLIME14")"
ensure_entry_after "SLIME15"  "SLIME14"  "$(mirror_line "SLIME15")"
ensure_entry_after "SLIME16"  "SLIME15"  "$(mirror_line "SLIME16")"

previous_lava_texture="LAVA4"
for lava_texture in "${lava_floor_textures[@]}"; do
    ensure_entry_after "$lava_texture" "$previous_lava_texture" "$(lava_line "$lava_texture")"
    previous_lava_texture="$lava_texture"
done

for lava_texture in "${lava_floor_textures[@]}"; do
    replace_entry "$lava_texture" "$(lava_line "$lava_texture")"
done

perl -pi -e 's/\{ "textureName":"NUKAGE([123])"\s*,"emissiveMult":1\.0\s*\}/{ "textureName":"NUKAGE$1"  ,"emissiveMult":1.0     ,"isMirror":true    ,"metallicDefault":1.0  ,"roughnessDefault":0.0 }/' "$textures"

copy_alias() {
    local source="$1"
    local dest="$2"

    if [[ -f "$mat_dir/$dest" ]]; then
        return 0
    fi
    if [[ ! -f "$mat_dir/$source" ]]; then
        echo "Missing source normal map $mat_dir/$source" >&2
        exit 1
    fi
    cp "$mat_dir/$source" "$mat_dir/$dest"
}

copy_alias FWATER1_n.ktx2 FLTWAWA1_n.ktx2
copy_alias FWATER2_n.ktx2 FLTWAWA2_n.ktx2
copy_alias FWATER3_n.ktx2 FLTWAWA3_n.ktx2
copy_alias FWATER1_n.ktx2 FLTFLWW1_n.ktx2
copy_alias FWATER2_n.ktx2 FLTFLWW2_n.ktx2
copy_alias FWATER3_n.ktx2 FLTFLWW3_n.ktx2
copy_alias FWATER1_n.ktx2 X_005_n.ktx2
copy_alias NUKAGE1_remix_normal.ktx2 NUKAGE1_n.ktx2
copy_alias NUKAGE2_remix_normal.ktx2 NUKAGE2_n.ktx2
copy_alias NUKAGE3_remix_normal.ktx2 NUKAGE3_n.ktx2
copy_alias LAVA1_remix_normal.ktx2 LAVA1_n.ktx2
copy_alias LAVA2_remix_normal.ktx2 LAVA2_n.ktx2
copy_alias LAVA3_remix_normal.ktx2 LAVA3_n.ktx2
copy_alias LAVA4_remix_normal.ktx2 LAVA4_n.ktx2
copy_alias FWATER1_n.ktx2 SWATER1_n.ktx2
copy_alias FWATER2_n.ktx2 SWATER2_n.ktx2
copy_alias FWATER3_n.ktx2 SWATER3_n.ktx2
copy_alias FWATER4_n.ktx2 SWATER4_n.ktx2
copy_alias SLIME01_n.ktx2 FLTSLUD1_n.ktx2
copy_alias SLIME02_n.ktx2 FLTSLUD2_n.ktx2
copy_alias SLIME03_n.ktx2 FLTSLUD3_n.ktx2
copy_alias SLIME01_n.ktx2 X_009_n.ktx2
for lava_texture in "${lava_floor_textures[@]}"; do
    rm -f "$mat_dir/${lava_texture}_n.ktx2"
done
copy_alias FWATER1_n.ktx2 F_WATR01_n.ktx2
copy_alias FWATER2_n.ktx2 F_WATR02_n.ktx2
copy_alias FWATER3_n.ktx2 F_WATR03_n.ktx2
copy_alias FWATER1_n.ktx2 F_HWATR1_n.ktx2
copy_alias FWATER2_n.ktx2 F_HWATR2_n.ktx2
copy_alias FWATER3_n.ktx2 F_HWATR3_n.ktx2
copy_alias FWATER1_n.ktx2 F_PWATR1_n.ktx2
copy_alias FWATER2_n.ktx2 F_PWATR2_n.ktx2
copy_alias FWATER3_n.ktx2 F_PWATR3_n.ktx2
copy_alias FWATER1_n.ktx2 P_VWATR1_n.ktx2
copy_alias FWATER2_n.ktx2 F_VWATR2_n.ktx2
copy_alias FWATER3_n.ktx2 F_VWATR3_n.ktx2
copy_alias SLIME09_remix_normal.ktx2 SLIME09_n.ktx2
copy_alias SLIME10_remix_normal.ktx2 SLIME10_n.ktx2
copy_alias SLIME11_remix_normal.ktx2 SLIME11_n.ktx2
copy_alias SLIME12_remix_normal.ktx2 SLIME12_n.ktx2
copy_alias SLIME13_remix_normal.ktx2 SLIME13_n.ktx2
copy_alias SLIME14_remix_normal.ktx2 SLIME14_n.ktx2
copy_alias SLIME15_remix_normal.ktx2 SLIME15_n.ktx2
copy_alias SLIME16_remix_normal.ktx2 SLIME16_n.ktx2

echo "Applied liquid RT material metadata and normal-map aliases to $rt_dir"
