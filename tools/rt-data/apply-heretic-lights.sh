#!/usr/bin/env bash
set -euo pipefail

rt_dir="${1:-rt}"
textures="$rt_dir/data/textures.json"

if [[ ! -f "$textures" ]]; then
    echo "Missing $textures" >&2
    echo "Usage: bash tools/rt-data/apply-heretic-lights.sh /path/to/rt" >&2
    exit 1
fi

backup="$textures.before-heretic-lights"
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

emissive_line() {
    printf '    ,   { "textureName":"%s"    ,"emissiveMult":%s     }' "$1" "$2"
}

light_line() {
    printf '    ,   { "textureName":"%s"    ,"emissiveMult":%s     ,"noShadow":true  ,"lightIntensity":%s  ,"lightColorHEX":"%s" }' "$1" "$2" "$3" "$4"
}

add_emissive_after() {
    ensure_entry_after "$1" "$2" "$(emissive_line "$1" "$3")"
}

add_light_after() {
    ensure_entry_after "$1" "$2" "$(light_line "$1" "$3" "$4" "$5")"
}

add_light_chain() {
    local anchor="$1"
    local emissive="$2"
    local intensity="$3"
    local color="$4"
    shift 4

    for texture in "$@"; do
        add_light_after "$texture" "$anchor" "$emissive" "$intensity" "$color"
        anchor="$texture"
    done
}

# Heretic pickups, keys, and artifacts.
add_emissive_after "SPMPA0" "SOULA0"  "0.2"
add_emissive_after "INVSA0" "SPMPA0"  "0.2"
add_emissive_after "PWBKA0" "INVSA0"  "0.2"
add_emissive_after "FBMBE0" "PWBKA0"  "0.15"
add_emissive_after "SHLDA0" "FBMBE0"  "0.15"
add_emissive_after "SHD2A0" "SHLDA0"  "0.2"
add_emissive_after "AKYYA0" "SHD2A0"  "0.4"
add_emissive_after "BKYYA0" "AKYYA0"  "0.4"
add_emissive_after "CKYYA0" "BKYYA0"  "0.4"
add_emissive_after "KGZBA0" "CKYYA0"  "0.35"
add_emissive_after "KGZGA0" "KGZBA0"  "0.35"
add_emissive_after "KGZYA0" "KGZGA0"  "0.35"

# Heretic ammo pickups. These mirror Heretic's GLDEFS light colors at a small
# item scale so ammo contributes to the RT scene instead of only glowing.
add_light_after "AMG1A0" "KGZYA0"  "0.2" "120" "ffff00"
add_light_chain "AMG1A0" "0.2" "240" "ffff00" "AMG2A0" "AMG2B0" "AMG2C0"
add_light_after "AMC1A0" "AMG2C0"  "0.2" "180" "00ff00"
add_light_chain "AMC1A0" "0.2" "240" "00ff00" "AMC2A0" "AMC2B0" "AMC2C0"
add_light_chain "AMC2C0" "0.2" "120" "0000ff" "AMB1A0" "AMB1B0" "AMB1C0"
add_light_chain "AMB1C0" "0.2" "240" "0000ff" "AMB2A0" "AMB2B0" "AMB2C0"
add_light_chain "AMB2C0" "0.2" "120" "ff0000" "AMS1A0" "AMS1B0"
add_light_chain "AMS1B0" "0.2" "240" "ff0000" "AMS2A0" "AMS2B0"
add_light_chain "AMS2B0" "0.2" "120" "ff9900" "AMP1A0" "AMP1B0" "AMP1C0"
add_light_chain "AMP1C0" "0.2" "240" "ff9900" "AMP2A0" "AMP2B0" "AMP2C0"

# First-person Heretic weapon frames that visibly flare during attacks.
add_emissive_after "GWNDC0" "SHTFB0"  "0.8"
add_emissive_after "GWNDD0" "GWNDC0"  "0.6"
add_emissive_after "CRBWD0" "GWNDD0"  "0.8"
add_emissive_after "CRBWE0" "CRBWD0"  "0.5"
add_emissive_after "CRBWF0" "CRBWE0"  "0.5"
add_emissive_after "CRBWG0" "CRBWF0"  "0.5"
add_emissive_after "CRBWH0" "CRBWG0"  "0.5"
add_emissive_after "BLSRD0" "CRBWH0"  "0.8"
add_emissive_after "PHNXC0" "BLSRD0"  "0.8"
add_emissive_after "MACEB0" "PHNXC0"  "0.5"
add_emissive_after "MACEC0" "MACEB0"  "0.5"
add_emissive_after "MACED0" "MACEC0"  "0.6"
add_emissive_after "MACEE0" "MACED0"  "0.5"
add_emissive_after "MACEF0" "MACEE0"  "0.5"
add_emissive_after "HRODA0" "MACEF0"  "0.7"
add_emissive_after "HRODG0" "HRODA0"  "0.8"
add_emissive_after "GAUND0" "HRODG0"  "0.5"
add_emissive_after "GAUNE0" "GAUND0"  "0.5"
add_emissive_after "GAUNF0" "GAUNE0"  "0.5"
add_emissive_after "GAUNL0" "GAUNF0"  "0.7"
add_emissive_after "GAUNM0" "GAUNL0"  "0.7"
add_emissive_after "GAUNN0" "GAUNM0"  "0.7"

# Heretic decorative lights.
add_light_after "CHDLA0" "SMRTD0" "0.2"  "700"  "fff06a"
add_light_after "CHDLB0" "CHDLA0" "0.2"  "700"  "fff06a"
add_light_after "CHDLC0" "CHDLB0" "0.2"  "700"  "fff06a"
add_light_after "KFR1A0" "CHDLC0" "0.25" "1000" "ffcc45"
add_light_after "KFR1B0" "KFR1A0" "0.25" "1000" "ffcc45"
add_light_after "KFR1C0" "KFR1B0" "0.25" "1000" "ffcc45"
add_light_after "KFR1D0" "KFR1C0" "0.25" "1000" "ffcc45"
add_light_after "KFR1E0" "KFR1D0" "0.25" "1000" "ffcc45"
add_light_after "KFR1F0" "KFR1E0" "0.25" "1000" "ffcc45"
add_light_after "KFR1G0" "KFR1F0" "0.25" "1000" "ffcc45"
add_light_after "KFR1H0" "KFR1G0" "0.25" "1000" "ffcc45"
add_light_after "SRTCA0" "KFR1H0" "0.2"  "800"  "ffcc45"
add_light_after "SRTCB0" "SRTCA0" "0.2"  "800"  "ffcc45"
add_light_after "SRTCC0" "SRTCB0" "0.2"  "800"  "ffcc45"
add_light_after "WTRHA0" "SRTCC0" "0.2"  "500"  "ffcc45"
add_light_after "WTRHB0" "WTRHA0" "0.2"  "500"  "ffcc45"
add_light_after "WTRHC0" "WTRHB0" "0.2"  "500"  "ffcc45"

# Heretic weapon projectiles and impact puffs.
for frame in A B C D; do
    add_light_after "PUF1${frame}0" "PUFFB0" "0.2" "500" "9b86ff"
done
for frame in E F G H; do
    add_light_after "PUF1${frame}0" "PUF1D0" "0.2" "600" "b2a0ff"
done
for frame in A B C D E; do
    add_light_after "PUF2${frame}0" "PUF1H0" "0.2" "450" "e6e680"
done
for frame in A B C D E F; do
    add_light_after "PUF4${frame}0" "PUF2E0" "0.2" "500" "ffdd85"
done
add_light_after "PUF3A0" "PUF4F0" "0.2" "450" "ffdd85"

for frame in A B C D E F G H; do
    add_light_after "FX01${frame}0" "PUF3A0" "0.2" "600" "e6e680"
done
for frame in A B C D E F G H I J; do
    add_light_after "FX03${frame}0" "FX01H0" "0.2" "900" "aaff80"
done
for frame in F G H I J; do
    add_light_after "FX02${frame}0" "FX03J0" "0.2" "700" "dddd88"
done
for frame in A B C D E F G; do
    add_light_after "FX17${frame}0" "FX02J0" "0.2" "600" "8080ff"
done
for frame in H I J K L M N O P Q R S; do
    add_light_after "FX18${frame}0" "FX17G0" "0.2" "900" "6060ff"
done
for frame in A B C D E F G H I J K L M; do
    add_light_after "FX00${frame}0" "FX18S0" "0.2" "900" "ff8080"
done
add_light_after "FX04A0" "FX00M0" "0.25" "1200" "ff9966"
for frame in A B C D E F G H; do
    add_light_after "FX08${frame}0" "FX04A0" "0.25" "1300" "ff9966"
done
for frame in A B C D E F G H I J K; do
    add_light_after "FX09${frame}0" "FX08H0" "0.25" "1000" "ff8844"
done
for frame in A B C D E F; do
    add_light_after "FX22${frame}0" "FX09K0" "0.2" "800" "ff4040"
done
for frame in A B C D E F; do
    add_light_after "XPL1${frame}0" "FX22F0" "0.2" "1000" "ff8844"
done

# Heretic enemy projectiles, monster attack/death flashes, and world effects
# from wadsrc_lights/static/filter/heretic/gldefs.txt.
add_light_chain "XPL1F0" "0.2" "800" "cc8054" "FX10A0" "FX10B0" "FX10C0" "FX10D0"
add_light_after "FX10E0" "FX10D0" "0.2" "1000" "cc8054"
add_light_after "FX10F0" "FX10E0" "0.2" "700" "996644"
add_light_after "FX10G0" "FX10F0" "0.2" "450" "4c1900"

add_light_chain "FX10G0" "0.2" "700" "ffff80" "FX15A0" "FX15B0" "FX15C0"
add_light_after "FX15D0" "FX15C0" "0.2" "900" "b3b359"
add_light_after "FX15E0" "FX15D0" "0.2" "1000" "666633"
add_light_after "FX15F0" "FX15E0" "0.2" "1100" "33331a"

add_light_chain "FX15F0" "0.2" "650" "66ff66" "SPAXA0" "SPAXB0" "SPAXC0"
add_light_after "SPAXD0" "SPAXC0" "0.2" "850" "4db34d"
add_light_after "SPAXE0" "SPAXD0" "0.2" "1000" "336633"
add_light_after "SPAXF0" "SPAXE0" "0.2" "1100" "003300"
add_light_chain "SPAXF0" "0.2" "650" "ff8080" "RAXEA0" "RAXEB0"
add_light_after "RAXEC0" "RAXEB0" "0.2" "850" "b34d4d"
add_light_after "RAXED0" "RAXEC0" "0.2" "1000" "663333"
add_light_after "RAXEE0" "RAXED0" "0.2" "1100" "331a1a"

add_light_chain "RAXEE0" "0.2" "650" "ff80ff" "FX11A0" "FX11B0" "FX11C0" "FX11D0" "FX11E0"
add_light_after "FX11F0" "FX11E0" "0.2" "400" "b34db3"
add_light_after "FX11G0" "FX11F0" "0.2" "250" "4c2b4c"

add_light_after "LICHD0" "FX11G0" "0.2" "1100" "ff6600"
add_light_after "LICHE0" "LICHD0" "0.2" "1300" "ffb300"
add_light_after "LICHF0" "LICHE0" "0.2" "1100" "cc6600"
add_light_after "LICHG0" "LICHF0" "0.2" "900" "660000"
add_light_chain "LICHG0" "0.2" "900" "6666ff" "FX05A0" "FX05B0" "FX05C0"
add_light_after "FX05D0" "FX05C0" "0.2" "1100" "6666ff"
add_light_after "FX05E0" "FX05D0" "0.2" "1000" "3333b3"
add_light_after "FX05F0" "FX05E0" "0.2" "850" "1a1a66"
add_light_after "FX05G0" "FX05F0" "0.2" "700" "000033"
add_light_chain "FX05G0" "0.2" "600" "000080" "FX05H0" "FX05I0" "FX05J0"
add_light_chain "FX05J0" "0.2" "900" "ffb366" "FX06A0" "FX06B0" "FX06C0"
add_light_after "FX06D0" "FX06C0" "0.2" "1000" "e6664d"
add_light_after "FX06E0" "FX06D0" "0.2" "900" "b31a33"
add_light_after "FX06F0" "FX06E0" "0.2" "750" "661a1a"
add_light_after "FX06G0" "FX06F0" "0.2" "600" "330000"

add_light_after "CLNKK0" "FX06G0" "0.2" "800" "ffcc00"
add_light_after "CLNKL0" "CLNKK0" "0.2" "1100" "ff9900"
add_light_after "CLNKM0" "CLNKL0" "0.2" "1000" "994d00"
add_light_after "CLNKN0" "CLNKM0" "0.2" "850" "4d0000"

add_light_chain "CLNKN0" "0.2" "950" "ffb300" "BEASI1" "BEASI2I8" "BEASI3I7"
add_light_chain "BEASI3I7" "0.2" "900" "ff804d" "FRB1A0" "FRB1B0" "FRB1C0"
add_light_after "FRB1D0" "FRB1C0" "0.2" "900" "cc663d"
add_light_after "FRB1E0" "FRB1D0" "0.2" "750" "994d33"
add_light_after "FRB1F0" "FRB1E0" "0.2" "600" "66331a"
add_light_after "FRB1G0" "FRB1F0" "0.2" "500" "330000"

add_light_chain "FRB1G0" "0.2" "450" "804dff" "SNFXA0" "SNFXB0" "SNFXC0" "SNFXD0"
add_light_after "SNFXE0" "SNFXD0" "0.2" "500" "804dff"
add_light_after "SNFXF0" "SNFXE0" "0.2" "450" "6633b3"
add_light_after "SNFXG0" "SNFXF0" "0.2" "500" "4d1a80"
add_light_after "SNFXH0" "SNFXG0" "0.2" "450" "4d004d"
add_light_chain "SNFXH0" "0.2" "650" "ff9966" "SNFXJ0" "SNFXK0"
add_light_after "SNFXL0" "SNFXK0" "0.2" "800" "ff9966"
add_light_after "SNFXM0" "SNFXL0" "0.2" "900" "994d40"
add_light_after "SNFXN0" "SNFXM0" "0.2" "800" "4d1a1a"

add_light_chain "SNFXN0" "0.2" "800" "ffb380" "FX12A0" "FX12B0"
add_light_after "FX12C0" "FX12B0" "0.2" "1000" "cc9966"
add_light_after "FX12D0" "FX12C0" "0.2" "1000" "cc664d"
add_light_after "FX12E0" "FX12D0" "0.2" "800" "994d33"
add_light_after "FX12F0" "FX12E0" "0.2" "800" "994d33"
add_light_after "FX12G0" "FX12F0" "0.2" "650" "661900"
add_light_after "FX12H0" "FX12G0" "0.2" "650" "661900"
add_light_chain "FX12H0" "0.2" "1000" "ffb380" "FX13B0" "FX13C0" "FX13D0" "FX13E0" "FX13F0" "FX13G0" "FX13H0"
add_light_after "FX13I0" "FX13H0" "0.2" "900" "ffb380"
add_light_after "FX13J0" "FX13I0" "0.2" "1000" "ffb380"
add_light_after "FX13K0" "FX13J0" "0.2" "1100" "b3664d"
add_light_after "FX13L0" "FX13K0" "0.2" "1200" "804d1a"
add_light_after "FX13M0" "FX13L0" "0.2" "1300" "330000"

add_light_chain "FX13M0" "0.2" "1000" "fff280" "FX14A0" "FX14B0" "FX14C0"
add_light_after "FX14D0" "FX14C0" "0.2" "1100" "fff280"
add_light_after "FX14E0" "FX14D0" "0.2" "1200" "cccc66"
add_light_after "FX14F0" "FX14E0" "0.2" "1400" "808040"
add_light_after "FX14G0" "FX14F0" "0.2" "1500" "33331a"
add_light_after "FX14H0" "FX14G0" "0.2" "1500" "33331a"
add_light_chain "FX14H0" "0.2" "1000" "8080ff" "FX16A0" "FX16B0" "FX16C0"
add_light_after "FX16G0" "FX16C0" "0.2" "1100" "8080ff"
add_light_after "FX16H0" "FX16G0" "0.2" "1300" "6666cc"
add_light_after "FX16I0" "FX16H0" "0.2" "1400" "4d4d99"
add_light_after "FX16J0" "FX16I0" "0.2" "1300" "333366"
add_light_after "FX16K0" "FX16J0" "0.2" "1300" "1a1a33"
add_light_after "FX16L0" "FX16K0" "0.2" "1300" "1a1a33"
add_light_chain "FX16L0" "0.2" "1100" "4d4dff" "SOR2R1" "SOR2R2" "SOR2R3" "SOR2R4" "SOR2R5" "SOR2R6" "SOR2R7" "SOR2R8" "SOR2S1" "SOR2S2" "SOR2S3" "SOR2S4" "SOR2S5" "SOR2S6" "SOR2S7" "SOR2S8" "SOR2T1" "SOR2T2" "SOR2T3" "SOR2T4" "SOR2T5" "SOR2T6" "SOR2T7" "SOR2T8"

add_light_after "PPODC0" "SOR2T8" "0.2" "900" "00ff00"
add_light_after "PPODD0" "PPODC0" "0.2" "1100" "00b300"
add_light_after "PPODE0" "PPODD0" "0.2" "1200" "006600"
add_light_after "PPODF0" "PPODE0" "0.2" "1300" "003300"
add_light_chain "PPODF0" "0.2" "1000" "ffb380" "VFBLA0" "VFBLB0"
add_light_chain "VFBLB0" "0.2" "800" "ff8000" "VTFBA0" "VTFBB0"
add_light_after "KGZ1A0" "VTFBB0" "0.2" "450" "bbbbff"
add_light_after "TELEA0" "KGZ1A0" "0.2" "1000" "6666ff"
add_light_after "TELEB0" "TELEA0" "0.2" "700" "6666ff"
add_light_after "TELEC0" "TELEB0" "0.2" "350" "6666ff"
add_light_chain "TELEC0" "0.2" "800" "8080ff" "TELED0" "TELEE0" "TELEF0"
add_light_chain "TELEF0" "0.2" "1000" "8080ff" "TELEG0" "TELEH0"

echo "Applied Heretic RT light metadata to $rt_dir"
