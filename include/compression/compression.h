// ============================================================================
// compression.h — NVM-aware dictionary compression for Spitfire
// ============================================================================
// v3: targets B+TREE LEAF PAGES, not heap pages.
//
// Discovery (Stage 2, step 4): Spitfire's YCSB table uses a ClusteredIndex,
// i.e. the tuples live in the B+tree leaves. The PartitionedHeapTable only
// stores OLD versions and is empty after a fresh load. The compaction pass
// therefore walks BTreeLeafNode pages.
//
// Leaf layout (verified against btreeolc.h):
//   [0,  16)      NodeBase   lsn(8) type(2) count(2) + 4 pad   <- kept RAW
//   [16, 24)      next       leaf chain pointer                <- kept RAW
//   [24, 15864)   data[15]   std::pair<uint64_t, YCSBTuple>, 1056 B each
//   [15864,16384) padding    520 B
//
// Each entry:
//   [0,   8)  pair key (search key)   \
//   [8,  48)  BaseTuple prefix         >  52 B kept RAW  -> the B+tree can
//   [48, 52)  YCSBTuple::key          /      still binary-search a compressed
//   [52, 1052) 10 columns x 100 B      -> dictionary-encoded    leaf page!
//   [1052,1056) alignment padding
//
// Compressed image:
//   [0,  24)  node header, untouched at its original position
//   [24, 32)  CompHeader
//   [32, ...) prefixes[15 * 52], then ids[15 * 10 * id_width]
//
// Scope: read-only workload. Dictionary built once by the compaction pass,
// immutable afterwards -> no locking on the read path.
// ============================================================================

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <utility>

#include "benchmark/ycsb/ycsb_configuration.h"   // YCSBTuple, COLUMN_COUNT
#include "buf/buf_mgr.h"                          // kPageSize

namespace spitfire {
namespace compression {

using benchmark::ycsb::YCSBTuple;

// ---- Verified layout constants ---------------------------------------------
constexpr size_t kColumnSize  = 100;
constexpr size_t kNumColumns  = COLUMN_COUNT;                       // 10
constexpr size_t kTupleSize   = sizeof(YCSBTuple);                  // 1048
constexpr size_t kEntrySize   = sizeof(std::pair<uint64_t, YCSBTuple>);  // 1056
constexpr size_t kPairKeySize = 8;
constexpr size_t kPrefixSize  = kPairKeySize + 44;                  // 52
constexpr size_t kColsOffInEntry = kPrefixSize;                     // 52

// NodeBase(16) + next(8); verified against btreeolc.h
constexpr size_t kLeafHeaderSize = 24;
constexpr size_t kEntriesPerLeaf = (kPageSize - kLeafHeaderSize) / kEntrySize; // 15
constexpr size_t kDataOff        = kLeafHeaderSize;                 // 24
constexpr size_t kDataEnd        = kDataOff + kEntriesPerLeaf * kEntrySize;    // 15864

static_assert(sizeof(YCSBTuple) == 1048, "tuple layout changed — revisit offsets");
static_assert(kEntrySize == 1056,        "pair layout changed — revisit offsets");
static_assert(kEntriesPerLeaf == 15,     "entries-per-leaf changed");

// ---- Compressed page header -------------------------------------------------
struct CompHeader {
    uint32_t magic;
    uint16_t num_entries;
    uint8_t  id_width;      // 1, 2 or 4
    uint8_t  flags;
};
static_assert(sizeof(CompHeader) == 8, "header must be 8 bytes");
constexpr uint32_t kCompressedMagic = 0xC0DEC0DE;

struct CompLayout {
    size_t hdr_off;         // = kLeafHeaderSize (24)
    size_t prefixes_off;
    size_t ids_off;
    size_t total;
    explicit CompLayout(uint8_t idw) {
        hdr_off      = kLeafHeaderSize;
        prefixes_off = hdr_off + sizeof(CompHeader);
        ids_off      = prefixes_off + kEntriesPerLeaf * kPrefixSize;
        total        = ids_off + kEntriesPerLeaf * kNumColumns * (size_t)idw;
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

extern DictionarySet g_dicts;

// ---- API --------------------------------------------------------------------
// Pass 1: register all column values of one raw leaf page.
void BuildDictionary(const uint8_t* raw_leaf, uint16_t num_entries);

// Pass 2: build the compressed image of one raw leaf into comp_leaf.
void EncodeLeaf(const uint8_t* raw_leaf, uint16_t num_entries, uint8_t* comp_leaf);

// Reconstructs original bytes [off, off+size) of the leaf into out.
//   Mode A: DecodeRange(leaf, 0, kPageSize, frame)
//   Mode B: DecodeRange(leaf, entry_off, kEntrySize, scratch)
void DecodeRange(const uint8_t* comp_leaf, size_t off, size_t size, char* out);

inline bool IsCompressed(const uint8_t* leaf) {
    return reinterpret_cast<const CompHeader*>(leaf + kLeafHeaderSize)->magic
           == kCompressedMagic;
}

inline size_t CompressedSize(const uint8_t* leaf) {
    const auto* h = reinterpret_cast<const CompHeader*>(leaf + kLeafHeaderSize);
    return CompLayout(h->id_width).total;
}

}  // namespace compression
}  // namespace spitfire