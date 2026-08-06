// ============================================================================
// compression.h: NVM-aware dictionary compression for Spitfire
// ============================================================================
// Ported from the standalone prototype (spitfire_rep.cpp, Stage 1).
// Uses Spitfire's REAL YCSBTuple type; the static_asserts guard that the
// layout the prototype was verified against still holds. If any assert fires
// at compile time, the layout has changed and the offset math must be revisited.
//
// Scope (Stage 2): read-only workload. Dictionary is built once during the
// post-load compaction pass and is IMMUTABLE afterwards (no locking needed on
// the read path). A write path for compressed pages is future work.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <unordered_map>
#include <string>
#include <algorithm>

#include "benchmark/ycsb/ycsb_configuration.h"   // YCSBTuple, COLUMN_COUNT
#include "buf/buf_mgr.h"                          // kPageSize

namespace spitfire {
namespace compression {

using benchmark::ycsb::YCSBTuple;

// ---- Verified layout constants (measured via sizecheck on the server) ------
constexpr size_t kColumnSize    = 100;                       // per-column bytes
constexpr size_t kNumColumns    = COLUMN_COUNT;              // = 10
constexpr size_t kTupleSize     = sizeof(YCSBTuple);         // must be 1048
constexpr size_t kPrefixSize    = 44;                        // BaseTuple(40)+key(4)
constexpr size_t kTuplesPerPage = (kPageSize - 2 - 8) / kTupleSize;  // = 15

static_assert(sizeof(YCSBTuple) == 1048, "tuple layout changed — revisit offsets");
static_assert(kTuplesPerPage == 15,      "tuples-per-page changed");

// HeapTablePage: tuples[] at offset 0, metadata at the END of the page
constexpr size_t kTuplesRegionEnd = kTuplesPerPage * kTupleSize;     // 15720
constexpr size_t kNextPagePidOff  = kTuplesRegionEnd;                // 15720
constexpr size_t kNumTuplesOff    = kNextPagePidOff + 8;             // 15728

// ---- Compressed page header -------------------------------------------------
struct CompHeader {
    uint32_t magic;
    uint16_t num_tuples;
    uint8_t  id_width;      // 1, 2 or 4
    uint8_t  flags;
};
static_assert(sizeof(CompHeader) == 8, "header must be 8 bytes");
constexpr uint32_t kCompressedMagic = 0xC0DEC0DE;

// Computed positions inside the compressed image
struct CompLayout {
    size_t prefixes_off;
    size_t ids_off;
    size_t tail_off;
    size_t total;
    explicit CompLayout(uint8_t idw) {
        prefixes_off = sizeof(CompHeader);
        ids_off      = prefixes_off + kTuplesPerPage * kPrefixSize;
        tail_off     = ids_off + kTuplesPerPage * kNumColumns * (size_t)idw;
        total        = tail_off + 8 /*pid*/ + 2 /*num_tuples*/;
    }
};

// ---- Dictionaries -----------------------------------------------------------
struct ColumnDictionary {
    std::vector<std::array<char, kColumnSize>> id_to_value;   // hot path
    std::unordered_map<std::string, uint32_t>  value_to_id;   // load phase only

    uint32_t GetOrInsert(const char* value) {
        std::string s(value, kColumnSize);
        auto it = value_to_id.find(s);
        if (it != value_to_id.end()) return it->second;
        uint32_t new_id = (uint32_t)id_to_value.size();
        std::array<char, kColumnSize> entry;
        memcpy(entry.data(), value, kColumnSize);
        id_to_value.push_back(entry);
        value_to_id.emplace(std::move(s), new_id);
        return new_id;
    }
    size_t Cardinality() const { return id_to_value.size(); }
};

struct DictionarySet {
    ColumnDictionary cols[kNumColumns];
    uint8_t RequiredIdWidth() const {
        size_t m = 0;
        for (auto& d : cols) m = std::max(m, d.Cardinality());
        if (m <= 0xFF)   return 1;
        if (m <= 0xFFFF) return 2;
        return 4;
    }
};

// ---- Global dictionary (one per table, immutable after load) ----------------
// Defined in compression.cpp. Built by the post-load compaction pass, read by
// both decode paths. Read-only after load -> no synchronization on reads.
extern DictionarySet g_dicts;

// ---- API --------------------------------------------------------------------
// Pass 1 of compaction: register all values of one raw page into g_dicts.
void BuildDictionary(const uint8_t* raw_page);

// Pass 2 of compaction: write the compressed image of one raw page into
// comp_page (may alias a scratch buffer; caller copies it back in place).
void EncodePage(const uint8_t* raw_page, uint8_t* comp_page);

// The unified decoder. Reconstructs original bytes [off, off+size) into out.
//   Mode A: DecodeRange(page, 0, kPageSize, frame)
//   Mode B: DecodeRange(page, pos*kTupleSize, kTupleSize, scratch)
void DecodeRange(const uint8_t* comp_page, size_t off, size_t size, char* out);

// Convenience: is this raw page already in compressed form?
inline bool IsCompressed(const uint8_t* page) {
    return reinterpret_cast<const CompHeader*>(page)->magic == kCompressedMagic;
}

}  // namespace compression
}  // namespace spitfire
