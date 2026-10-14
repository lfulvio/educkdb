#!/bin/sh
# Replaces c_src/duckdb with the amalgamation of a DuckDB release, split into
# parts. As one file, DuckDB 1.5.6 needs more than 13 GB of memory to compile
# at -O3; in 8 parts each compiles in at most ~4 GB.
#
#   ./update-duckdb.sh v1.5.6 [splits]
set -eu

VERSION=${1:?usage: $0 vX.Y.Z [splits]}
SPLITS=${2:-8}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$VERSION" https://github.com/duckdb/duckdb.git "$TMP/duckdb"
(cd "$TMP/duckdb" && python3 scripts/amalgamation.py --splits="$SPLITS" > /dev/null)
AMALGAMATION="$TMP/duckdb/src/amalgamation"

# Every part includes duckdb-internal.hpp, which gathers all internal headers,
# glibc's <sys/mman.h> among them. Its MAP_TYPE macro then breaks code using
# MAP_TYPE as a template parameter. DuckDB undefines it the same way in
# src/common/encryption_key_manager.cpp.
printf '\n#ifdef MAP_TYPE\n#undef MAP_TYPE\n#endif\n' >> "$AMALGAMATION/duckdb-internal.hpp"
# platform.hpp defines DuckDBPlatform() in the header, meant for a single
# translation unit; with every part including it, the link sees one copy per
# part. Inline lets the linker keep one.
sed -i 's/^std::string DuckDBPlatform() {/inline std::string DuckDBPlatform() {/' "$AMALGAMATION/duckdb-internal.hpp"
grep -q '^inline std::string DuckDBPlatform() {' "$AMALGAMATION/duckdb-internal.hpp"

rm -f c_src/duckdb/*
cp "$AMALGAMATION"/duckdb*.cpp "$AMALGAMATION"/duckdb*.hpp c_src/duckdb/
cp "$TMP/duckdb/src/include/duckdb.h" "$TMP/duckdb/src/include/duckdb_extension.h" c_src/duckdb/
echo "c_src/duckdb: DuckDB $VERSION in $SPLITS parts"
