// ============================================================================
// compression.cpp: implementation of the NVM-aware dictionary compression
// ============================================================================
#include "compression/compression.h"

#include <cassert>

namespace spitfire {
namespace compression {

// The one global dictionary set for the user table.
DictionarySet g_dicts;

// ---------------------------------------------------------------------------
void BuildDictionary(const uint8_t* raw_page) {
    const auto* tuples = reinterpret_cast<const YCSBTuple*>(raw_page);
    for (size_t t = 0; t < kTuplesPerPage; t++)
        for (size_t c = 0; c < kNumColumns; c++)
            g_dicts.cols[c].GetOrInsert(tuples[t].cols[c]);
}

// ---------------------------------------------------------------------------
void EncodePage(const uint8_t* raw_page, uint8_t* comp_page) {
    const auto* tuples = reinterpret_cast<const YCSBTuple*>(raw_page);
    const uint8_t idw = g_dicts.RequiredIdWidth();
    const CompLayout L(idw);
    assert(L.total <= kPageSize);

    memset(comp_page, 0, kPageSize);
    auto* hdr = reinterpret_cast<CompHeader*>(comp_page);
    hdr->magic      = kCompressedMagic;
    hdr->num_tuples = (uint16_t)kTuplesPerPage;
    hdr->id_width   = idw;
    hdr->flags      = 0;

    // raw 44-byte prefixes, verbatim
    for (size_t t = 0; t < kTuplesPerPage; t++)
        memcpy(comp_page + L.prefixes_off + t * kPrefixSize,
               raw_page + t * kTupleSize, kPrefixSize);

    // dictionary IDs
    for (size_t t = 0; t < kTuplesPerPage; t++)
        for (size_t c = 0; c < kNumColumns; c++) {
            uint32_t id = g_dicts.cols[c].GetOrInsert(tuples[t].cols[c]);
            memcpy(comp_page + L.ids_off + (t * kNumColumns + c) * idw, &id, idw);
        }

    // tail metadata copy
    memcpy(comp_page + L.tail_off,     raw_page + kNextPagePidOff, 8);
    memcpy(comp_page + L.tail_off + 8, raw_page + kNumTuplesOff,   2);
}

// ---------------------------------------------------------------------------
void DecodeRange(const uint8_t* comp_page, size_t off, size_t size, char* out) {
    const auto* hdr = reinterpret_cast<const CompHeader*>(comp_page);
    assert(hdr->magic == kCompressedMagic);
    const uint8_t idw = hdr->id_width;
    const CompLayout L(idw);

    size_t cur = off;
    const size_t end = off + size;
    assert(end <= kPageSize);

    while (cur < end) {
        if (cur >= kTuplesRegionEnd) {
            // tail region: metadata copy or padding
            for (size_t p = cur; p < end; p++) {
                if (p >= kNextPagePidOff && p < kNextPagePidOff + 8)
                    out[p - off] = comp_page[L.tail_off + (p - kNextPagePidOff)];
                else if (p >= kNumTuplesOff && p < kNumTuplesOff + 2)
                    out[p - off] = comp_page[L.tail_off + 8 + (p - kNumTuplesOff)];
                else
                    out[p - off] = 0;
            }
            break;
        }

        size_t tuple_idx = cur / kTupleSize;
        size_t rem       = cur % kTupleSize;
        size_t tuple_end = (tuple_idx + 1) * kTupleSize;
        size_t stop      = std::min(end, std::min(tuple_end, kTuplesRegionEnd));

        while (cur < stop) {
            if (rem < kPrefixSize) {
                // raw prefix bytes
                size_t n = std::min(stop - cur, kPrefixSize - rem);
                memcpy(out + (cur - off),
                       comp_page + L.prefixes_off + tuple_idx * kPrefixSize + rem, n);
                cur += n; rem += n;
            } else if (rem >= kPrefixSize + kNumColumns * kColumnSize) {
                // per-tuple alignment padding (1044..1047): zeros
                size_t n = std::min(stop - cur, kTupleSize - rem);
                memset(out + (cur - off), 0, n);
                cur += n; rem += n;
            } else {
                // column bytes: read only the id, look up the value
                size_t col        = (rem - kPrefixSize) / kColumnSize;
                size_t within_col = (rem - kPrefixSize) % kColumnSize;
                uint32_t id = 0;
                memcpy(&id, comp_page + L.ids_off +
                            (tuple_idx * kNumColumns + col) * idw, idw);
                const char* value = g_dicts.cols[col].id_to_value[id].data();
                size_t n = std::min(stop - cur, kColumnSize - within_col);
                memcpy(out + (cur - off), value + within_col, n);
                cur += n; rem += n;
            }
        }
    }
}

}  // namespace compression
}  // namespace spitfire
