#!/bin/bash
# differential fuzz test of the compressor: the working tree must compress
#   every random case to the same bytes as a reference commit, and decompress back.
# usage: ./fuzz_test.sh <ref commit> [count=2000] [jobs=4] [seed=1]
#   SANITIZE=1 also builds the working tree with -fsanitize=address,undefined.
#   needs HDiffPatch cloned at ../HDiffPatch, like the Makefile.
set -e
cd "$(dirname "$0")"
REF=${1:?"usage: $0 <ref commit> [count] [jobs] [seed]"}
COUNT=${2:-2000}
JOBS=${3:-4}
SEED=${4:-1}
CXX=${CXX:-c++}
HDP=$(cd ../HDiffPatch && pwd)
TMP=$(mktemp -d)
trap 'git worktree remove --force "$TMP/ref" >/dev/null 2>&1; rm -rf "$TMP"' EXIT

git worktree add --detach -q "$TMP/ref" "$REF"
ln -s "$HDP" "$TMP/HDiffPatch" # the sources include ../HDiffPatch

build(){ # build <tinyuz dir> <out> [flags...]
    local SRC=$1 OUT=$2; shift 2
    $CXX -w -O2 -D_IS_USED_MULTITHREAD=1 -D_HPATCH_IS_USED_MULTITHREAD=0 "$@" -I"$SRC" -o "$OUT" \
        fuzz_test.cpp "$SRC"/compress/tuz_enc.cpp "$SRC"/compress/tuz_enc_private/*.cpp \
        "$HDP"/libHDiffPatch/HDiff/private_diff/libdivsufsort/divsufsort.cpp \
        "$HDP"/libParallel/parallel_channel.cpp \
        -x c "$HDP"/libParallel/parallel_import_c.c "$SRC"/decompress/tuz_dec.c \
        "$HDP"/libHDiffPatch/HPatch/patch.c -lpthread
}
build "$TMP/ref" "$TMP/fuzz_ref" -DNDEBUG
build . "$TMP/fuzz_new" -DNDEBUG
if [ "$SANITIZE" = "1" ]; then
    build . "$TMP/fuzz_san" -g -fsanitize=address,undefined -fno-sanitize-recover=all
fi

run(){ # run <binary> <out file>: COUNT seeds split into JOBS parts
    local BIN=$1 OUT=$2 per=$(( (COUNT+JOBS-1)/JOBS )) j
    for (( j=0; j<JOBS; ++j )); do
        local n=$(( COUNT-j*per<per ? COUNT-j*per : per ))
        [ $n -gt 0 ] && { "$BIN" $(( SEED+j*per )) $n > "$OUT.$j" || true; } &
    done
    wait
    cat "$OUT".* > "$OUT"
}
run "$TMP/fuzz_ref" "$TMP/ref.txt"
run "$TMP/fuzz_new" "$TMP/new.txt"
[ "$SANITIZE" = "1" ] && run "$TMP/fuzz_san" "$TMP/san.txt"

ret=0
for f in "$TMP"/new.txt "$TMP"/san.txt; do
    [ -f "$f" ] || continue
    if [ $(grep -c ' ok$' "$f") -ne $COUNT ]; then
        echo "error: $(basename "$f" .txt) failed or didn't finish:"
        grep -v ' ok$' "$f" | head -5
        ret=1
    fi
    if ! cmp -s "$TMP/ref.txt" "$f"; then
        echo "error: $(basename "$f" .txt) output differs from $REF:"
        diff "$TMP/ref.txt" "$f" | head -10
        ret=1
    fi
done
[ $ret -eq 0 ] && echo "fuzz_test ok: $COUNT cases same as $REF (seeds $SEED..$(( SEED+COUNT-1 )))"
exit $ret
