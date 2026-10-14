// See https://raw.githubusercontent.com/duckdb/duckdb/main/LICENSE for licensing information

#include "duckdb.hpp"
#include "duckdb-internal.hpp"
#ifndef DUCKDB_AMALGAMATION
#error header mismatch
#endif


// This is a dummy loader to produce a workable duckdb library without linking any extensions.
// Link this to libduckdb_static.a to get a working system.

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &db) {
	// nop
}

vector<string> ExtensionHelper::LoadedExtensionTestPaths() {
	return {};
}

} // namespace duckdb
