// Compression Prototype v2 (Stage 1, updated) — Master's Thesis


#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <array>
#include <unordered_map>
#include <string>
#include <random>
#include <algorithm>


// The real tuple & page layout (replicated from Spitfire, verified)


#define COLUMN_COUNT 10
#define COLUMN_SIZE  100

struct TuplePointerReplica { uint64_t pid; uint64_t pos; };            // 16 B
struct BaseTupleReplica {
    uint64_t            row_id;                                        
    TuplePointerReplica next_tuple_ptr;                               
    TuplePointerReplica header_ptr;                                 
};

struct YCSBTuple : BaseTupleReplica {
    uint32_t key;
    char cols[COLUMN_COUNT][COLUMN_SIZE];
};


static_assert(sizeof(BaseTupleReplica) == 40, "BaseTuple prefix must be 40 B");
static_assert(sizeof(YCSBTuple) == 1048,      "real tuple size is 1048 B");

constexpr size_t kPrefixSize   = 44;  
constexpr size_t kColsOff      = 44;  

constexpr size_t kPageSize      = 1 << 14;                         
constexpr size_t kTupleSize     = sizeof(YCSBTuple);             
constexpr size_t kTuplesPerPage = 15;   

constexpr size_t kTuplesRegionEnd = kTuplesPerPage * kTupleSize;     
constexpr size_t kNextPagePidOff  = kTuplesRegionEnd;                
constexpr size_t kNumTuplesOff    = kNextPagePidOff + 8;         
static_assert(kTuplesRegionEnd == 15720, "layout drift");
static_assert((kPageSize - 2 - 8) / kTupleSize == 15, "HeapTablePage math");

// The compressed page format (v2)

struct CompHeader {
    uint32_t magic;
    uint16_t num_tuples;
    uint8_t  id_width;  
    uint8_t  flags;
};
static_assert(sizeof(CompHeader) == 8, "header must be 8 bytes");
constexpr uint32_t kCompressedMagic = 0xC0DEC0DE;

struct CompLayout {
    size_t prefixes_off;  
    size_t ids_off;        
    size_t tail_off;    
    size_t total;      
    CompLayout(uint8_t idw) {
        prefixes_off = sizeof(CompHeader);
        ids_off      = prefixes_off + kTuplesPerPage * kPrefixSize;
        tail_off     = ids_off + kTuplesPerPage * COLUMN_COUNT * (size_t)idw;
        total        = tail_off + 8 /*pid*/ + 2 /*num_tuples*/;
    }
};


// Dictionaries

struct ColumnDictionary {
    std::vector<std::array<char, COLUMN_SIZE>> id_to_value;  
    std::unordered_map<std::string, uint32_t>  value_to_id;  

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
        size_t m = 0;
        for (auto& d : cols) m = std::max(m, d.Cardinality());
        if (m <= 0xFF)   return 1;
        if (m <= 0xFFFF) return 2;
        return 4;
    }
};


// encode: raw HeapTablePage image -> compressed image

void encode(const uint8_t* raw_page, DictionarySet& dicts, uint8_t* comp_page) {
    const auto* tuples = reinterpret_cast<const YCSBTuple*>(raw_page);

    for (size_t t = 0; t < kTuplesPerPage; t++)
        for (int c = 0; c < COLUMN_COUNT; c++)
            dicts.cols[c].GetOrInsert(tuples[t].cols[c]);

    uint8_t idw = dicts.RequiredIdWidth();
    CompLayout L(idw);
    assert(L.total <= kPageSize);

    memset(comp_page, 0, kPageSize);
    auto* hdr = reinterpret_cast<CompHeader*>(comp_page);
    hdr->magic = kCompressedMagic;
    hdr->num_tuples = kTuplesPerPage;
    hdr->id_width = idw;
    hdr->flags = 0;

    for (size_t t = 0; t < kTuplesPerPage; t++)
        memcpy(comp_page + L.prefixes_off + t * kPrefixSize,
               raw_page + t * kTupleSize, kPrefixSize);

    for (size_t t = 0; t < kTuplesPerPage; t++)
        for (int c = 0; c < COLUMN_COUNT; c++) {
            uint32_t id = dicts.cols[c].GetOrInsert(tuples[t].cols[c]);
            memcpy(comp_page + L.ids_off + (t * COLUMN_COUNT + c) * idw,
                   &id, idw);   // little-endian truncation
        }

    memcpy(comp_page + L.tail_off,     raw_page + kNextPagePidOff, 8);
    memcpy(comp_page + L.tail_off + 8, raw_page + kNumTuplesOff,   2);
}

// decode_range: THE unified decoder (Mode A and Mode B)

void decode_range(const uint8_t* comp_page, const DictionarySet& dicts,
                  size_t off, size_t size, uint8_t* out) {
    const auto* hdr = reinterpret_cast<const CompHeader*>(comp_page);
    assert(hdr->magic == kCompressedMagic);
    const uint8_t idw = hdr->id_width;
    const CompLayout L(idw);

    size_t cur = off;                    
    const size_t end = off + size;
    assert(end <= kPageSize);

    while (cur < end) {
        if (cur >= kTuplesRegionEnd) {
            size_t n = end - cur;
            for (size_t i = 0; i < n; i++) {
                size_t p = cur + i;
                if (p >= kNextPagePidOff && p < kNextPagePidOff + 8)
                    out[cur - off + i] = comp_page[L.tail_off + (p - kNextPagePidOff)];
                else if (p >= kNumTuplesOff && p < kNumTuplesOff + 2)
                    out[cur - off + i] = comp_page[L.tail_off + 8 + (p - kNumTuplesOff)];
                else
                    out[cur - off + i] = 0;
            }
            cur = end;
            break;
        }

        size_t tuple_idx = cur / kTupleSize;
        size_t rem       = cur % kTupleSize;
        size_t tuple_end = (tuple_idx + 1) * kTupleSize;
        size_t stop      = std::min(end, std::min(tuple_end, kTuplesRegionEnd));

        while (cur < stop) {
            if (rem < kPrefixSize) {
                size_t n = std::min(stop - cur, kPrefixSize - rem);
                memcpy(out + (cur - off),
                       comp_page + L.prefixes_off + tuple_idx * kPrefixSize + rem,
                       n);
                cur += n; rem += n;
            } else if (rem >= kColsOff + COLUMN_COUNT * COLUMN_SIZE) {
                size_t n = std::min(stop - cur, kTupleSize - rem);
                memset(out + (cur - off), 0, n);
                cur += n; rem += n;
            } else {
                size_t col        = (rem - kColsOff) / COLUMN_SIZE;
                size_t within_col = (rem - kColsOff) % COLUMN_SIZE;
                assert(col < COLUMN_COUNT);
                uint32_t id = 0;
                memcpy(&id, comp_page + L.ids_off +
                            (tuple_idx * COLUMN_COUNT + col) * idw, idw);
                const char* value = dicts.cols[col].id_to_value[id].data();
                size_t n = std::min(stop - cur, COLUMN_SIZE - within_col);
                memcpy(out + (cur - off), value + within_col, n);
                cur += n; rem += n;
            }
        }
    }
}

inline void decode_full(const uint8_t* comp_page, const DictionarySet& dicts,
                        uint8_t* frame) {
    decode_range(comp_page, dicts, 0, kPageSize, frame);
}

// Test data generator — builds a RAW HeapTablePage image

void generate_raw_page(uint8_t* raw_page, size_t cardinality, uint32_t seed) {
    memset(raw_page, 0, kPageSize);
    auto* tuples = reinterpret_cast<YCSBTuple*>(raw_page);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> pick(0, (uint32_t)cardinality - 1);

    for (size_t t = 0; t < kTuplesPerPage; t++) {
 
        tuples[t].row_id            = 0xAA00 + t;
        tuples[t].next_tuple_ptr    = { 0xBB00 + t, t };
        tuples[t].header_ptr        = { 0xCC00 + t, t * 2 };
        tuples[t].key               = (uint32_t)t * 7 + 1000;
        for (int c = 0; c < COLUMN_COUNT; c++) {
            uint32_t v = pick(rng);
            memset(tuples[t].cols[c], 0, COLUMN_SIZE);
            snprintf(tuples[t].cols[c], COLUMN_SIZE, "C%d_V%08u_", c, v);
        }
    }
    uint64_t next_pid = 0xDEADBEEF;
    uint16_t num_t    = kTuplesPerPage;
    memcpy(raw_page + kNextPagePidOff, &next_pid, 8);
    memcpy(raw_page + kNumTuplesOff,   &num_t,    2);
}

//  main: the proof (extended tests)

int main() {
    printf("=== Compression prototype v2: round-trip tests (real layout) ===\n\n");
    printf("tuple=%zuB  prefix=%zuB  tuples/page=%zu  tail@%zu\n\n",
           kTupleSize, kPrefixSize, kTuplesPerPage, kTuplesRegionEnd);
    printf("%-13s %-9s %-10s %-9s  %s\n",
           "Cardinality", "ID width", "Compr.(B)", "Ratio", "Tests");

    std::mt19937 rng(1337);  

    for (size_t card : {2, 10, 100, 300, 70000, 100000}) {
        std::vector<uint8_t> raw(kPageSize), comp(kPageSize);
        generate_raw_page(raw.data(), card, /*seed=*/42);

        DictionarySet dicts;
        encode(raw.data(), dicts, comp.data());

        std::vector<uint8_t> frame(kPageSize, 0xAB);
        decode_full(comp.data(), dicts, frame.data());
        bool t1 = (memcmp(frame.data(), raw.data(), kPageSize) == 0);

        bool t2 = true;
        std::vector<uint8_t> scratch(kTupleSize);
        for (size_t pos = 0; pos < kTuplesPerPage && t2; pos++) {
            decode_range(comp.data(), dicts, pos * kTupleSize, kTupleSize,
                         scratch.data());
            t2 = (memcmp(scratch.data(), raw.data() + pos * kTupleSize,
                         kTupleSize) == 0);
        }

        uint8_t buf8[8], buf2[2];
        decode_range(comp.data(), dicts, kNextPagePidOff, 8, buf8);
        decode_range(comp.data(), dicts, kNumTuplesOff,   2, buf2);
        bool t3 = memcmp(buf8, raw.data() + kNextPagePidOff, 8) == 0 &&
                  memcmp(buf2, raw.data() + kNumTuplesOff,   2) == 0;

        bool t4 = true;
        std::vector<uint8_t> rbuf(kPageSize);
        for (int i = 0; i < 200 && t4; i++) {
            size_t o = rng() % kPageSize;
            size_t s = 1 + rng() % (kPageSize - o);
            decode_range(comp.data(), dicts, o, s, rbuf.data());
            t4 = (memcmp(rbuf.data(), raw.data() + o, s) == 0);
        }

        CompLayout L(dicts.RequiredIdWidth());
        printf("%-13zu %-9u %-10zu %6.1f:1  [%s][%s][%s][%s]\n",
               card, dicts.RequiredIdWidth(), L.total,
               (double)kPageSize / L.total,
               t1 ? "OK" : "T1-FAIL", t2 ? "OK" : "T2-FAIL",
               t3 ? "OK" : "T3-FAIL", t4 ? "OK" : "T4-FAIL");
        if (!(t1 && t2 && t3 && t4)) return 1;
    }

    printf("\nAll round-trip tests passed (full page, whole tuples, tail, random).\n");
    return 0;
}