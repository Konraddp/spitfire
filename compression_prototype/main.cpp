
// compression prototype (stage 1): masters thesis


#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <array>
#include <unordered_map>
#include <string>
#include <random>

// The original tuple format

#define COLUMN_COUNT 10
#define COLUMN_SIZE  100

struct YCSBTuple {
    uint32_t key;
    char cols[COLUMN_COUNT][COLUMN_SIZE];
};

constexpr size_t kPageSize = 1 << 14;

constexpr size_t kTuplesPerPage = kPageSize / sizeof(YCSBTuple);

struct PageHeader {
    uint32_t magic;       
    uint16_t num_tuples;  
    uint8_t  id_width;     
    uint8_t  flags;        
};
static_assert(sizeof(PageHeader) == 8, "Header must be exactly 8 bytes");

constexpr uint32_t kCompressedMagic = 0xC0DEC0DE;

// The dictionaries

struct ColumnDictionary {
    std::vector<std::array<char, COLUMN_SIZE>> id_to_value;
    std::unordered_map<std::string, uint32_t> value_to_id;

    uint32_t GetOrInsert(const char* value) {
        std::string s(value, COLUMN_SIZE);
        auto it = value_to_id.find(s);
        if (it != value_to_id.end()) return it->second;
        uint32_t new_id = (uint32_t)id_to_value.size();
        std::array<char, COLUMN_SIZE> entry;
        memcpy(entry.data(), value, COLUMN_SIZE);
        id_to_value.push_back(entry);
        value_to_id.emplace(std::move(s), new_id);
        return new_id;
    }

    size_t Cardinality() const { return id_to_value.size(); }
};

struct DictionarySet {
    ColumnDictionary cols[COLUMN_COUNT];

    uint8_t RequiredIdWidth() const {
        size_t max_card = 0;
        for (auto& d : cols) max_card = std::max(max_card, d.Cardinality());
        if (max_card <= 0xFF)   return 1; 
        if (max_card <= 0xFFFF) return 2; 
        return 4;
    }
};


std::vector<uint8_t> encode(const YCSBTuple* tuples, size_t n, DictionarySet& dicts) {
    for (size_t t = 0; t < n; t++)
        for (int c = 0; c < COLUMN_COUNT; c++)
            dicts.cols[c].GetOrInsert(tuples[t].cols[c]);

    uint8_t idw = dicts.RequiredIdWidth();

    size_t page_bytes = sizeof(PageHeader) + n * 4 /*keys*/ + n * COLUMN_COUNT * idw;
    std::vector<uint8_t> page(page_bytes, 0);

    auto* hdr = reinterpret_cast<PageHeader*>(page.data());
    hdr->magic      = kCompressedMagic;
    hdr->num_tuples = (uint16_t)n;
    hdr->id_width   = idw;
    hdr->flags      = 0;

    uint8_t* keys_out = page.data() + sizeof(PageHeader);
    for (size_t t = 0; t < n; t++)
        memcpy(keys_out + t * 4, &tuples[t].key, 4);

    uint8_t* ids_out = keys_out + n * 4;
    for (size_t t = 0; t < n; t++) {
        for (int c = 0; c < COLUMN_COUNT; c++) {
            uint32_t id = dicts.cols[c].GetOrInsert(tuples[t].cols[c]);
            memcpy(ids_out + (t * COLUMN_COUNT + c) * idw, &id, idw);
        }
    }
    return page;
}

// decode_full: Mode A

void decode_full(const uint8_t* comp_page, const DictionarySet& dicts,
                 uint8_t* dram_frame /* kPageSize large */) {
    auto* hdr = reinterpret_cast<const PageHeader*>(comp_page);
    assert(hdr->magic == kCompressedMagic);
    size_t  n   = hdr->num_tuples;
    uint8_t idw = hdr->id_width;

    const uint8_t* keys_in = comp_page + sizeof(PageHeader);
    const uint8_t* ids_in  = keys_in + n * 4;

    auto* out = reinterpret_cast<YCSBTuple*>(dram_frame);
    for (size_t t = 0; t < n; t++) {
        memcpy(&out[t].key, keys_in + t * 4, 4);
        for (int c = 0; c < COLUMN_COUNT; c++) {
            uint32_t id = 0;
            memcpy(&id, ids_in + (t * COLUMN_COUNT + c) * idw, idw);
            memcpy(out[t].cols[c], dicts.cols[c].id_to_value[id].data(), COLUMN_SIZE);
        }
    }
    size_t used = n * sizeof(YCSBTuple);
    memset(dram_frame + used, 0, kPageSize - used);
}

// decode_selective: Mode B


struct Slice {              
    const char* data;
    size_t      size;
};

Slice decode_selective(const uint8_t* comp_page, const DictionarySet& dicts,
                       size_t off, size_t size) {
    auto* hdr = reinterpret_cast<const PageHeader*>(comp_page);
    assert(hdr->magic == kCompressedMagic);
    uint8_t idw = hdr->id_width;
    size_t  n   = hdr->num_tuples;

    size_t tuple_idx = off / sizeof(YCSBTuple);
    size_t rem       = off % sizeof(YCSBTuple);
    assert(tuple_idx < n);

    const uint8_t* keys_in = comp_page + sizeof(PageHeader);
    const uint8_t* ids_in  = keys_in + n * 4;

    if (rem < 4) {
        // Access to the key itself: stored uncompressed in the page
        assert(rem + size <= 4);
        return { reinterpret_cast<const char*>(keys_in + tuple_idx * 4 + rem), size };
    }

    size_t col        = (rem - 4) / COLUMN_SIZE;
    size_t within_col = (rem - 4) % COLUMN_SIZE;
    assert(col < COLUMN_COUNT);
    assert(within_col + size <= COLUMN_SIZE && "access must not cross a column boundary");

    // The actual selective access
    uint32_t id = 0;
    memcpy(&id, ids_in + (tuple_idx * COLUMN_COUNT + col) * idw, idw);

    // Zero-copy return: pointer into the dictionary
    const char* value = dicts.cols[col].id_to_value[id].data();
    return { value + within_col, size };
}


// Part 7: Test data generator

void generate_tuples(YCSBTuple* out, size_t n, size_t cardinality, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> pick(0, (uint32_t)cardinality - 1);
    for (size_t t = 0; t < n; t++) {
        out[t].key = (uint32_t)t * 7 + 1000;
        for (int c = 0; c < COLUMN_COUNT; c++) {
            uint32_t v = pick(rng);
            memset(out[t].cols[c], 0, COLUMN_SIZE);
            snprintf(out[t].cols[c], COLUMN_SIZE, "C%d_V%08u_", c, v);
        }
    }
}

// main: the proof

int main() {
    printf("=== Compression prototype: round-trip tests ===\n\n");
    printf("%-14s %-10s %-14s %-14s %-8s\n",
           "Cardinality", "ID width", "Raw (B)", "Compr. (B)", "Ratio");

    for (size_t card : {2, 10, 100, 300, 70000, 100000}) {

        std::vector<YCSBTuple> tuples(kTuplesPerPage);
        generate_tuples(tuples.data(), kTuplesPerPage, card, /*seed=*/42);

        DictionarySet dicts;
        auto comp = encode(tuples.data(), kTuplesPerPage, dicts);

        std::vector<uint8_t> frame(kPageSize, 0xAB);   
        decode_full(comp.data(), dicts, frame.data());

        std::vector<uint8_t> original(kPageSize, 0);
        memcpy(original.data(), tuples.data(), kTuplesPerPage * sizeof(YCSBTuple));
        bool full_ok = (memcmp(frame.data(), original.data(), kPageSize) == 0);

        bool sel_ok = true;
        for (size_t t = 0; t < kTuplesPerPage && sel_ok; t++) {
            // Test key access
            size_t key_off = t * sizeof(YCSBTuple);
            Slice ks = decode_selective(comp.data(), dicts, key_off, 4);
            if (memcmp(ks.data, &tuples[t].key, 4) != 0) sel_ok = false;
            for (int c = 0; c < COLUMN_COUNT && sel_ok; c++) {
                size_t off = t * sizeof(YCSBTuple) + 4 + (size_t)c * COLUMN_SIZE;
                Slice s = decode_selective(comp.data(), dicts, off, COLUMN_SIZE);
                if (memcmp(s.data, tuples[t].cols[c], COLUMN_SIZE) != 0) sel_ok = false;
            }
        }

        // Result row
        size_t raw_bytes = kTuplesPerPage * sizeof(YCSBTuple);
        auto* hdr = reinterpret_cast<PageHeader*>(comp.data());
        printf("%-14zu %-10u %-14zu %-14zu %6.1f:1   [%s] full  [%s] selective\n",
               card, hdr->id_width, raw_bytes, comp.size(),
               (double)raw_bytes / comp.size(),
               full_ok ? "OK" : "FAIL",
               sel_ok  ? "OK" : "FAIL");

        if (!full_ok || !sel_ok) return 1;   // abort immediately on failure
    }

    printf("\nAll round-trip tests passed.\n");
    return 0;
}