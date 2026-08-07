// ============================================================================
// compression.cpp — dictionary compression on B+tree leaf pages
// ============================================================================
#include "compression/compression.h"

#include <cassert>
#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace spitfire {
namespace compression {

DictionarySet g_dicts;

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
void DecodeRange(const uint8_t *comp_leaf, size_t off, size_t size, char *out) {
    const auto *hdr = reinterpret_cast<const CompHeader *>(comp_leaf + kLeafHeaderSize);
    assert(hdr->magic == kCompressedMagic);
    const uint8_t idw = hdr->id_width;
    const size_t  n   = hdr->num_entries;
    const CompLayout L(idw);

    size_t cur = off;
    const size_t end = off + size;
    assert(end <= kPageSize);

    while (cur < end) {
        // ---- node header region: raw, in place ----
        if (cur < kDataOff) {
            size_t k = std::min(end, kDataOff) - cur;
            memcpy(out + (cur - off), comp_leaf + cur, k);
            cur += k;
            continue;
        }
        // ---- trailing padding ----
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

        // entries beyond count are unused -> zeros
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
}

}  // namespace compression
}  // namespace spitfire