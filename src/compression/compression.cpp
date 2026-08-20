// ============================================================================
// compression.cpp: dictionary compression on B+tree leaf pages
// ============================================================================
#include "compression/compression.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace spitfire {
namespace compression {

DictionarySet g_dicts;
Counters      g_counters;

// Helper: pointer to the columns of entry `e` inside a RAW leaf page.
static inline const char *RawEntryCols(const uint8_t *raw_leaf, size_t e, size_t col) {
    return reinterpret_cast<const char *>(
        raw_leaf + kDataOff + e * kEntrySize + kColsOffInEntry + col * kColumnSize);
}

// ---------------------------------------------------------------------------
void BuildDictionary(const uint8_t *raw_leaf, uint16_t num_entries) {
    for (size_t e = 0; e < num_entries && e < kEntriesPerLeaf; e++)
        for (size_t c = 0; c < kNumColumns; c++)
            g_dicts.cols[c].GetOrInsert(RawEntryCols(raw_leaf, e, c));
}

// ---------------------------------------------------------------------------
void EncodeLeaf(const uint8_t *raw_leaf, uint16_t num_entries, uint8_t *comp_leaf) {
    const uint8_t idw = g_dicts.RequiredIdWidth();
    const CompLayout L(idw);
    assert(L.total <= kPageSize);

    // node header (NodeBase + next) stays byte-identical at its position
    memcpy(comp_leaf, raw_leaf, kLeafHeaderSize);
    memset(comp_leaf + kLeafHeaderSize, 0, kPageSize - kLeafHeaderSize);

    auto *hdr = reinterpret_cast<CompHeader *>(comp_leaf + L.hdr_off);
    hdr->magic       = kCompressedMagic;
    hdr->num_entries = num_entries;
    hdr->id_width    = idw;
    hdr->flags       = 0;

    for (size_t e = 0; e < num_entries && e < kEntriesPerLeaf; e++) {
        // raw prefix: pair key + BaseTuple + tuple.key  (52 B)
        memcpy(comp_leaf + L.prefixes_off + e * kPrefixSize,
               raw_leaf + kDataOff + e * kEntrySize, kPrefixSize);
        // dictionary ids
        for (size_t c = 0; c < kNumColumns; c++) {
            uint32_t id = g_dicts.cols[c].GetOrInsert(RawEntryCols(raw_leaf, e, c));
            memcpy(comp_leaf + L.ids_off + (e * kNumColumns + c) * idw, &id, idw);
        }
    }
}

// ---------------------------------------------------------------------------
// M3: bitmap of touched 64 B cache lines within one page (16384/64 = 256 lines).
// Deduplication is essential: several partial accesses often hit the SAME line
// (e.g. all 10 ids of an entry at id_width=1 live in a single line). Only the
// first access generates bus traffic, the rest are L1 hits. Summing per-access
// cache lines would overstate mode B by roughly 4x and destroy the comparison.
// ---------------------------------------------------------------------------
namespace {
struct LineSet {
    uint64_t w[kPageSize / kCacheLine / 64] = {};   // 256 lines -> 4 words
    void Mark(size_t off, size_t n) {
        if (n == 0) return;
        size_t first = off / kCacheLine;
        size_t last  = (off + n - 1) / kCacheLine;
        for (size_t i = first; i <= last; ++i) w[i >> 6] |= 1ull << (i & 63);
    }
    uint64_t Bytes() const {
        uint64_t c = 0;
        for (uint64_t x : w) c += (uint64_t)__builtin_popcountll(x);
        return c * kCacheLine;
    }
};
}  // namespace

// ---------------------------------------------------------------------------
void DecodeRange(const uint8_t *comp_leaf, size_t off, size_t size, char *out) {
    const auto *hdr = reinterpret_cast<const CompHeader *>(comp_leaf + kLeafHeaderSize);
    assert(hdr->magic == kCompressedMagic);
    const uint8_t idw = hdr->id_width;
    const size_t  n   = hdr->num_entries;
    const CompLayout L(idw);

    size_t cur = off;
    const size_t end = off + size;
    assert(end <= kPageSize);

#ifdef M3_TIMING
    const auto t_start = std::chrono::steady_clock::now();
#endif
    LineSet  lines;
    uint64_t local_lookups = 0;
    // the compressed header itself is always touched
    lines.Mark(kLeafHeaderSize, sizeof(CompHeader));

    while (cur < end) {
        // ---- node header region: raw, in place ----
        if (cur < kDataOff) {
            size_t k = std::min(end, kDataOff) - cur;
            memcpy(out + (cur - off), comp_leaf + cur, k);
            lines.Mark(cur, k);
            cur += k;
            continue;
        }
        // ---- trailing padding: written from thin air, page not read ----
        if (cur >= kDataEnd) {
            memset(out + (cur - off), 0, end - cur);
            break;
        }

        // ---- entry region ----
        size_t rel        = cur - kDataOff;
        size_t entry_idx  = rel / kEntrySize;
        size_t rem        = rel % kEntrySize;
        size_t entry_end  = kDataOff + (entry_idx + 1) * kEntrySize;
        size_t stop       = std::min(end, std::min(entry_end, kDataEnd));

        // entries beyond count are unused -> zeros, page not read
        if (entry_idx >= n) {
            memset(out + (cur - off), 0, stop - cur);
            cur = stop;
            continue;
        }

        while (cur < stop) {
            if (rem < kPrefixSize) {
                size_t k = std::min(stop - cur, kPrefixSize - rem);
                memcpy(out + (cur - off),
                       comp_leaf + L.prefixes_off + entry_idx * kPrefixSize + rem, k);
                lines.Mark(L.prefixes_off + entry_idx * kPrefixSize + rem, k);
                cur += k; rem += k;
            } else if (rem >= kColsOffInEntry + kNumColumns * kColumnSize) {
                size_t k = std::min(stop - cur, kEntrySize - rem);
                memset(out + (cur - off), 0, k);      // per-entry alignment pad
                cur += k; rem += k;
            } else {
                size_t col        = (rem - kColsOffInEntry) / kColumnSize;
                size_t within_col = (rem - kColsOffInEntry) % kColumnSize;
                uint32_t id = 0;
                memcpy(&id, comp_leaf + L.ids_off +
                            (entry_idx * kNumColumns + col) * idw, idw);
                lines.Mark(L.ids_off + (entry_idx * kNumColumns + col) * idw, idw);
                ++local_lookups;

                // diagnostic guard: an out-of-range id means we decoded a page
                // that is not actually one of ours
                if (col >= kNumColumns || id >= g_dicts.cols[col].id_to_value.size()) {
                    fprintf(stderr,
                        "DECODE ERROR: off=%zu size=%zu entry=%zu col=%zu id=%u "
                        "dict_size=%zu hdr{magic=%08x n=%u idw=%u} node_type=%u\n",
                        off, size, entry_idx, col, id,
                        col < kNumColumns ? g_dicts.cols[col].id_to_value.size() : (size_t)0,
                        hdr->magic, (unsigned)hdr->num_entries, (unsigned)idw,
                        (unsigned)*reinterpret_cast<const uint16_t*>(comp_leaf + 8));
                    abort();
                }

                const char *value = g_dicts.cols[col].id_to_value[id].data();
                size_t k = std::min(stop - cur, kColumnSize - within_col);
                memcpy(out + (cur - off), value + within_col, k);
                cur += k; rem += k;
            }
        }
    }

    // ---- M3: flush per-call counters -------------------------------------
    // bytes_read covers the COMPRESSED PAGE only, not the ~100 B read from the
    // in-DRAM dictionary; those are accounted for separately via dict_lookups,
    // so both cost components stay visible and separable.
    g_counters.bytes_read   += lines.Bytes();
    g_counters.dict_lookups += local_lookups;
    g_counters.decode_calls += 1;
#ifdef M3_TIMING
    g_counters.decode_ns += (uint64_t)std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t_start).count();
#endif
}

}  // namespace compression
}  // namespace spitfire