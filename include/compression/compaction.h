// ============================================================================
// compaction.h — post-load compression pass
// ============================================================================
// Runs once after LoadYCSBDatabase, in two passes over all data pages:
//   Pass 1: register every column value in the global dictionary
//           (so that id_width is FINAL before any page is written)
//   Pass 2: rewrite each page in place as its compressed image
//
// Pages become self-describing via the magic header, so a page can travel
// between tiers (NVM <-> SSD) unchanged; the decode hooks check the magic.
//
// Compaction is in-place: a compressed page still occupies its 16 KB slot.
// This is deliberate — the thesis measures ACCESS COST (bytes crossing the
// bus), not storage footprint. Reclaiming the freed space would require
// changing Spitfire's page allocation and is out of scope.
// ============================================================================

#pragma once

#include "buf/buf_mgr.h"

namespace spitfire {
namespace compression {

// Compresses all data pages of the YCSB user table. Call once, after loading,
// before the benchmark starts. No-op if state.comp_mode == 0 (off).
void CompactYCSBTable(ConcurrentBufferManager *buf_mgr);

}  // namespace compression
}  // namespace spitfire
