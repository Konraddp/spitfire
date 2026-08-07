// ============================================================================
// ycsb_validation.h: cross-validation between compression modes
// ============================================================================
// Reads EVERY tuple exactly once, in key order, and folds key + all column
// bytes into one order-independent checksum. Two runs that load identical
// data must produce an identical checksum, whatever the compression mode.
//
// Why not checksum the normal workload: RunBackend seeds its RNG from
// rand()/time(0), so two runs never touch the same key sequence. A sequential
// full scan is both deterministic AND stricter — it verifies every tuple,
// not just the randomly hit ones.
// ============================================================================

#pragma once

#include "buf/buf_mgr.h"

namespace spitfire {
namespace benchmark {
namespace ycsb {

// Returns the checksum and logs it. Called when -V is given.
uint64_t ValidateTableChecksum(ConcurrentBufferManager *buf_mgr);

}  // namespace ycsb
}  // namespace benchmark
}  // namespace spitfire
