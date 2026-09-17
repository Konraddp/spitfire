// ============================================================================
// ycsb_validation.cpp: cross-validation between compression modes
// ============================================================================
#include "benchmark/ycsb/ycsb_validation.h"
#include "benchmark/ycsb/ycsb_configuration.h"
#include "benchmark/ycsb/ycsb_loader.h"
#include "engine/executor.h"
#include "util/logger.h"

namespace spitfire {
namespace benchmark {
namespace ycsb {

// FNV-1a over the whole logical tuple content (key + all columns).
static uint64_t HashTuple(const YCSBTuple &t) {
    uint64_t h = 1469598103934665603ULL;
    auto feed = [&h](const void *p, size_t n) {
        const uint8_t *b = reinterpret_cast<const uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    };
    feed(&t.key, sizeof(t.key));
    for (int c = 0; c < COLUMN_COUNT; ++c)
        feed(t.cols[c], sizeof(t.cols[c]));
    return h;
}
// The new column values come from the same generator the loader uses, with the
// row identifier shifted by one, so every written value is already present in
// the dictionary. This is what makes the update a stage-1 update; a general
// update workload would introduce unseen values and require dictionary growth.
size_t ApplyDeterministicUpdates(ConcurrentBufferManager *buf_mgr, int stride) {
    auto txn_manager = MVTOTransactionManager::GetInstance(buf_mgr);
    const int tuple_count = state.scale_factor * 1000;
    size_t updated = 0, failed = 0;

    for (int k = 0; k < tuple_count; k += stride) {
        const uint64_t lookup_key = (uint64_t)k;
        TransactionContext *txn = txn_manager->BeginTransaction(0);

        PointUpdateExecutor<uint64_t, YCSBTuple> upd(
            *user_table, lookup_key,
            [lookup_key](const YCSBTuple &t) { return t.key == lookup_key; },
            [](YCSBTuple &t) {
                for (int c = 0; c < COLUMN_COUNT; ++c) {
                    memset(t.cols[c], 0, sizeof(t.cols[c]));
                    if (state.cardinality > 0) {
                        uint32_t v = (uint32_t)(((t.key + 1) * 2654435761u
                                                 + c * 40503u) % state.cardinality);
                        snprintf(t.cols[c], sizeof(t.cols[c]), "C%d_V%08u_", c, v);
                    } else {
                        memset(t.cols[c], 1, sizeof(t.cols[c]));
                    }
                }
            },
            txn, buf_mgr);

        if (upd.Execute()) {
            txn_manager->CommitTransaction(txn);
            ++updated;
        } else {
            txn_manager->AbortTransaction(txn);
            ++failed;
        }
    }

    LOG_INFO("VALIDATION updates: stride=%d updated=%zu failed=%zu",
             stride, updated, failed);
    return updated;
}

uint64_t ValidateTableChecksum(ConcurrentBufferManager *buf_mgr) {
    auto txn_manager = MVTOTransactionManager::GetInstance(buf_mgr);
    const int tuple_count = state.scale_factor * 1000;

    uint64_t checksum = 0;   // XOR of per-tuple hashes -> order independent
    size_t found = 0, missing = 0;

    for (int k = 0; k < tuple_count; ++k) {
        const uint64_t lookup_key = (uint64_t)k;
        TransactionContext *txn = txn_manager->BeginTransaction(0);

        uint64_t tuple_hash = 0;
        bool got = false;

        IndexScanExecutor<uint64_t, YCSBTuple> lookup(
            *user_table, lookup_key, /*point_lookup=*/true,
            [&](const YCSBTuple &t, bool &should_end_scan) {
                if (t.key == lookup_key) {
                    should_end_scan = true;
                    tuple_hash = HashTuple(t);
                    got = true;
                    return true;
                }
                return false;
            },
            /*acquire_owner=*/false, txn, buf_mgr);

        bool res = lookup.Execute();
        if (!res) {
            txn_manager->AbortTransaction(txn);
            ++missing;
            continue;
        }
        txn_manager->CommitTransaction(txn);

        if (got) { checksum ^= tuple_hash; ++found; }
        else     { ++missing; }
    }

    LOG_INFO("VALIDATION mode=%d cardinality=%d tuples=%d found=%zu missing=%zu "
             "checksum=%016lx",
             state.comp_mode, state.cardinality, tuple_count,
             found, missing, (unsigned long)checksum);
    return checksum;
}

}  // namespace ycsb
}  // namespace benchmark
}  // namespace spitfire
