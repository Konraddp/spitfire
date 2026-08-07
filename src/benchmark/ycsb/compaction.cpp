// ============================================================================
// compaction.cpp — post-load compression pass
// ============================================================================
#include "compression/compaction.h"
#include "compression/compression.h"

#include "benchmark/ycsb/ycsb_loader.h"        // user_table, YCSBTable
#include "benchmark/ycsb/ycsb_configuration.h" // state
#include "util/logger.h"

#include <vector>

namespace spitfire {
namespace compression {

void CompactYCSBTable(ConcurrentBufferManager *buf_mgr) {
    using benchmark::ycsb::state;
    using benchmark::ycsb::user_table;

    if (state.comp_mode == 0) return;          // -C off -> vanilla, nothing to do
    if (user_table == nullptr) {
        LOG_ERROR("CompactYCSBTable: user_table is null");
        return;
    }

    auto &index = user_table->GetPrimaryIndex();

    // Pass 1
    size_t leaves_seen = 0;
    index.ForEachLeaf([&](pid_t pid, char *leaf) {
        auto *raw = reinterpret_cast<const uint8_t *>(leaf);
        uint16_t count = *reinterpret_cast<const uint16_t *>(raw + 10);  // NodeBase.count
        BuildDictionary(raw, count);
        leaves_seen++;
    });

    const uint8_t idw = g_dicts.RequiredIdWidth();
    LOG_INFO("compaction pass 1: %zu pages, id_width=%u, cardinalities: "
             "c0=%zu c1=%zu ... c9=%zu",
             leaves_seen, (unsigned)idw,
             g_dicts.cols[0].Cardinality(),
             g_dicts.cols[1].Cardinality(),
             g_dicts.cols[9].Cardinality());

    // Pass 2
    std::vector<uint8_t> scratch(kPageSize);
    size_t leaves_encoded = 0;
    index.ForEachLeaf([&](pid_t pid, char *leaf) {
        (void)pid;
        auto *raw = reinterpret_cast<uint8_t *>(leaf);
        if (IsCompressed(raw)) return;
        uint16_t count = *reinterpret_cast<const uint16_t *>(raw + 10);
        EncodeLeaf(raw, count, scratch.data());
        memcpy(leaf, scratch.data(), kPageSize);
        leaves_encoded++;
    });

    const CompLayout L(idw);
    LOG_INFO("compaction pass 2: %zu pages compressed, %zu B used per page "
             "(ratio %.1f:1 on page content)",
             leaves_encoded, L.total, (double)kPageSize / (double)L.total);
}

}  // namespace compression
}  // namespace spitfire
