#!/bin/bash
# Build the separate RP Duo Mac variant from the pinned release engine tree.
set -euo pipefail

if [[ $# != 2 ]]; then
    echo 'usage: bundle-mac-engine.sh ENGINE_TREE RESOURCES_DIRECTORY' >&2
    exit 2
fi

integration="$(cd "$(dirname "$0")" && pwd)"
tools_dir="$integration/../../frontend/tools"
source_tree="$1"
destination="$2/rpduo-engine"
base_id="$(cat "$source_tree/.engine-id")"
if [[ "$base_id" != ccd834e06a3938834bb5579e2ccc6a0ad13d3970-4466493482b5 ]]; then
    exit 0
fi

test ! -e "$destination"
work="$(mktemp -d "$2/.rpduo-engine.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT
stage="$work/tree"
ditto "$source_tree" "$stage"
video="$integration/patches/DirectProducer-v1.4.0.patch"
haptics="$integration/patches/DirectHaptics-v1.4.0.patch"
header="$integration/source/nx2apple/dm/host/rpduo_haptics_mac.h"
patch -s -N -p1 -d "$stage" < "$video"
patch -s -N -p1 -d "$stage" < "$haptics"
cp "$header" "$stage/dm/host/"

digest="$({ printf '%s\n' "$base_id"; cat "$video" "$haptics" "$header"; } | shasum -a 256 | cut -c1-12)"
printf '%s\n' "$base_id" > "$stage/.rpduo-base-engine-id"
printf '%s-%s\n' "${base_id%-*}" "$digest" > "$stage/.engine-id"
ditto "$stage" "$work/bundle"
"$tools_dir/sign_engine_tree.sh" "$work/bundle" "$stage"
mv "$work/bundle" "$destination"
echo "RP Duo engine in the bundle: $(cat "$destination/.engine-id")"
