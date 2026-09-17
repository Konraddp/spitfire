// ============================================================================
// ycsb_validation.h: cross-validation between compression modes
// ============================================================================
// Reads EVERY tuple exactly once, in key order, and folds key + all column
// bytes into one order-independent checksum. Two runs that load identical
// data must produce an identical checksum, whatever the compression mode.
//
// Why not checksum the normal workload: RunBackend seeds its RNG from
// rand()/time(0), so two runs never touch the same key sequence. A sequential
// full scan is both deterministic AND stricter. It verifies every tuple,
// not just the randomly hit ones.
// ============================================================================
// Write-path validation: with an update ratio greater than zero, a fixed
// update sequence is applied before the checksum is taken. Because the
// sequence is identical in every arm, a matching checksum then also shows
// that the compressed write path wrote the right identifier to the right
// place, checked against two independent references.

#pragma once

#include "buf/buf_mgr.h"

namespace spitfire {
namespace benchmark {
namespace ycsb {

// Applies a fixed, deterministic update to every stride-th tuple before the
// checksum is taken. The sequence contains no randomness and no Zipf draw, so
// all three arms perform byte-identical updates and their checksums remain
// comparable. Returns the number of tuples actually updated.
size_t ApplyDeterministicUpdates(ConcurrentBufferManager *buf_mgr, int stride = 16);

// Returns the checksum and logs it. Called when -V is given.
uint64_t ValidateTableChecksum(ConcurrentBufferManager *buf_mgr);

}  // namespace ycsb
}  // namespace benchmark
}  // namespace spitfire