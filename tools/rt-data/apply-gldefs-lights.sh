#!/usr/bin/env bash
set -euo pipefail

rt_dir="${1:-rt}"
textures="$rt_dir/data/textures.json"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

if [[ ! -f "$textures" ]]; then
    echo "Missing $textures" >&2
    echo "Usage: bash tools/rt-data/apply-gldefs-lights.sh /path/to/rt" >&2
    exit 1
fi

backup="$textures.before-gldefs-lights"
if [[ ! -f "$backup" ]]; then
    cp "$textures" "$backup"
fi

entries="$(mktemp)"
tmp="$(mktemp)"
trap 'rm -f "$entries" "$tmp"' EXIT

perl -MFile::Find -MList::Util=uniq -e '
    use strict;
    use warnings;

    my ($repo_root, $textures, $out) = @ARGV;

    my %existing;
    {
        open my $fh, "<", $textures or die "open $textures: $!";
        local $/;
        my $json = <$fh>;
        while ($json =~ /"textureName":"([^"]+)"/g) {
            $existing{$1} = 1;
        }
    }

    my (%by5, %by4);
    my $remember = sub {
        my ($name) = @_;
        return unless defined $name && $name =~ /^[A-Z0-9_]+$/;
        push @{ $by5{substr($name, 0, 5)} }, $name if length($name) >= 5;
        push @{ $by4{substr($name, 0, 4)} }, $name if length($name) >= 5;
    };

    for my $bm (glob("$repo_root/wadsrc_bm/static/filter/*/gldefs.bm")) {
        open my $fh, "<", $bm or next;
        while (<$fh>) {
            $remember->($1) if /^\s*brightmap\s+sprite\s+([A-Z0-9_]+)/;
        }
    }

    my @zscript;
    find(
        sub {
            push @zscript, $File::Find::name if -f $_ && /\.(?:zs|zc)$/;
        },
        "$repo_root/wadsrc/static/zscript"
    );

    for my $zs (@zscript) {
        open my $fh, "<", $zs or next;
        while (<$fh>) {
            s{//.*}{};
            next unless /^\s*([A-Z0-9]{4})\s+([A-Z]+)\b/;
            my ($sprite, $frames) = ($1, $2);
            for my $frame (uniq split //, $frames) {
                $remember->("$sprite${frame}0");
            }
        }
    }

    my (%lights, @frames);
    for my $gldefs (glob("$repo_root/wadsrc_lights/static/filter/*/gldefs.txt")) {
        open my $fh, "<", $gldefs or next;
        my ($current, $color, $size);
        while (<$fh>) {
            if (/^\s*(?:pointlight|flickerlight\d*|pulselight)\s+([A-Za-z0-9_]+)/) {
                ($current, $color, $size) = ($1, undef, undef);
                next;
            }

            if (defined $current) {
                $color = [$1, $2, $3] if /^\s*color\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)/i;
                $size = $1 if /^\s*size\s+([0-9.]+)/i;
                if (/^\s*}/) {
                    $lights{$current} = {
                        color => $color || [1.0, 1.0, 1.0],
                        size  => defined($size) ? $size : 48,
                    };
                    undef $current;
                }
                next;
            }

            while (/frame\s+([A-Z0-9_]+)\s*\{\s*light\s+([A-Za-z0-9_]+)/g) {
                push @frames, [$1, $2];
            }
        }
    }

    my %emitted;
    my $hex = sub {
        my ($rgb) = @_;
        return join "", map {
            my $v = int(($_ * 255) + 0.5);
            $v = 0 if $v < 0;
            $v = 255 if $v > 255;
            sprintf "%02x", $v;
        } @$rgb;
    };

    my $intensity = sub {
        my ($size) = @_;
        my $v = int(($size * 12.5) + 0.5);
        $v = 80 if $v < 80;
        $v = 3000 if $v > 3000;
        return $v;
    };

    my $candidates = sub {
        my ($frame) = @_;
        my @names;

        push @names, @{ $by5{$frame} || [] };
        push @names, @{ $by4{$frame} || [] } if length($frame) == 4;

        if (length($frame) >= 5) {
            push @names, "${frame}0";
        }

        return uniq grep { /^[A-Z0-9_]+$/ } @names;
    };

    open my $outfh, ">", $out or die "open $out: $!";
    for my $pair (@frames) {
        my ($frame, $light) = @$pair;
        my $meta = $lights{$light} or next;
        my $color = $hex->($meta->{color});
        my $intensity = $intensity->($meta->{size});

        for my $texture ($candidates->($frame)) {
            next if $existing{$texture} || $emitted{$texture};
            $emitted{$texture} = 1;
            printf $outfh
                "    ,   { \"textureName\":\"%s\"    ,\"emissiveMult\":0.2     ,\"noShadow\":true  ,\"lightIntensity\":%-5d,\"lightColorHEX\":\"%s\" }\n",
                $texture, $intensity, $color;
        }
    }
' "$repo_root" "$textures" "$entries"

if [[ ! -s "$entries" ]]; then
    echo "No new GLDEFS-derived RT light metadata needed for $rt_dir"
    exit 0
fi

awk -v entries="$entries" '
    BEGIN {
        while ((getline line < entries) > 0) {
            block = block line "\n"
        }
        close(entries)
    }
    !inserted && $0 ~ /^[[:space:]]*\][[:space:]]*$/ {
        printf "%s", block
        inserted = 1
    }
    { print }
    END {
        if (!inserted) {
            exit 2
        }
    }
' "$textures" > "$tmp" || {
    echo "Could not find textures.json array close in $textures" >&2
    exit 1
}

mv "$tmp" "$textures"
echo "Applied GLDEFS-derived RT light metadata to $rt_dir"
