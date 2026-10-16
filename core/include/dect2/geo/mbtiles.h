// Read-only MBTiles reader (a SQLite file with a `tiles` table of zoom_level, tile_column, tile_row, tile_data), written from the public
// SQLite file-format description so no SQLite library is needed (it works the same on every platform).
//
// What it reads: the b-trees of the `tiles` table and, when there is one on (zoom_level, tile_column, tile_row), its index, so a lookup
// touches a handful of pages. Without such an index a lookup scans the table (slow for big files, still correct). Overflow pages are followed.
// What it does not read: the "deduplicated" layout where `tiles` is a view over `map` and `images`; WAL-mode files with a live -wal; databases
// that are not UTF-8/page-size sane. Tile bytes are returned as stored (PNG or JPEG for raster maps; a vector .pbf is gzip data and is of no
// use to the raster map, see isRaster()). Every read is bounds checked: a damaged file gives false, never a crash.
// MBTiles stores rows in TMS order: tile_row = 2^z - 1 - y for the usual XYZ y of the map; tile() takes the XYZ y.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "dect2/common/endian.h"

namespace dect2 {

class MbTiles {
public:
    MbTiles() = default;
    ~MbTiles() { close(); }
    MbTiles(const MbTiles&) = delete;
    MbTiles& operator=(const MbTiles&) = delete;

    bool open(const std::string& path, std::string* err = nullptr) {
        close();
        std::lock_guard<std::mutex> lk(mu_);
        f_ = fopen(path.c_str(), "rb");
        if (!f_) return fail(err, "cannot open the file");
        uint8_t h[100];
        if (fread(h, 1, 100, f_) != 100 || memcmp(h, "SQLite format 3", 16) != 0) return closeFail(err, "not an SQLite file");
        pageSize_ = (h[16] << 8) | h[17];
        if (pageSize_ == 1) pageSize_ = 65536;
        if (pageSize_ < 512 || pageSize_ > 65536 || (pageSize_ & (pageSize_ - 1))) return closeFail(err, "bad SQLite page size");
        reserved_ = h[20];
        usable_ = pageSize_ - reserved_;
        if (usable_ < 480) return closeFail(err, "bad SQLite page layout");
        if (h[18] == 2 || h[19] == 2) return closeFail(err, "WAL-mode file: open it once in a tool that checkpoints it");
        const uint32_t enc = be32(h + 56);
        if (enc != 0 && enc != 1) return closeFail(err, "text is not UTF-8");
        fseek(f_, 0, SEEK_END);
        const long sz = ftell(f_);
        pages_ = sz > 0 ? (uint32_t)(sz / pageSize_) : 0;
        if (pages_ == 0) return closeFail(err, "empty file");
        if (!readSchema()) return closeFail(err, "no usable `tiles` table (a deduplicated map/images layout is not supported)");
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lk(mu_);
        if (f_) fclose(f_);
        f_ = nullptr; tilesRoot_ = indexRoot_ = 0; meta_.clear();
    }
    bool isOpen() const { return f_ != nullptr; }
    bool hasIndex() const { return indexRoot_ != 0; }

    // The tile (XYZ y) as stored; false when there is none
    bool tile(int z, int x, int y, std::vector<uint8_t>& out) {
        std::lock_guard<std::mutex> lk(mu_);
        out.clear();
        if (!f_ || z < 0 || z > 30 || x < 0 || y < 0) return false;
        const int64_t n = (int64_t)1 << z;
        if (x >= n || y >= n) return false;
        const int64_t row = n - 1 - y;
        if (indexRoot_) {
            int64_t rowid = 0;
            if (!indexFind(z, x, row, rowid)) return false;
            return tableRowTile(rowid, out);
        }
        return scanTile(z, x, row, out);
    }

    // The rows of the `metadata` table (name -> value), e.g. "name", "format", "minzoom", "maxzoom", "attribution"
    std::string meta(const std::string& key) const {
        for (const auto& kv : meta_) if (kv.first == key) return kv.second;
        return std::string();
    }
    // True when the bytes are a picture the raster map can draw (PNG or JPEG), not a vector tile
    static bool isRaster(const std::vector<uint8_t>& d) {
        if (d.size() >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return true;
        if (d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return true;
        return false;
    }
    static bool isPng(const std::vector<uint8_t>& d) { return d.size() >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G'; }

    // varint of the SQLite record format: up to 9 bytes. Returns the bytes used (0 when it runs past `end`).
    static int varint(const uint8_t* p, const uint8_t* end, uint64_t& v) {
        v = 0;
        for (int i = 0; i < 8; i++) {
            if (p + i >= end) return 0;
            v = (v << 7) | (p[i] & 0x7F);
            if (!(p[i] & 0x80)) return i + 1;
        }
        if (p + 8 >= end) return 0;
        v = (v << 8) | p[8];
        return 9;
    }

private:
    struct Col { bool isNull = true, isInt = false, isBlob = false; int64_t i = 0; std::string s; std::vector<uint8_t> b; };

    FILE* f_ = nullptr;
    std::mutex mu_;
    uint32_t pageSize_ = 0, usable_ = 0, pages_ = 0, reserved_ = 0;
    uint32_t tilesRoot_ = 0, indexRoot_ = 0;
    std::vector<std::pair<std::string, std::string>> meta_;

    static bool fail(std::string* e, const char* m) { if (e) *e = m; return false; }
    bool closeFail(std::string* e, const char* m) { if (f_) fclose(f_); f_ = nullptr; return fail(e, m); }
    static uint32_t be32(const uint8_t* p) { return (uint32_t)dect2::endian::loadBe32(p); }
    static uint32_t be16(const uint8_t* p) { return (uint32_t)dect2::endian::loadBe16(p); }

    bool page(uint32_t no, std::vector<uint8_t>& buf) {
        if (no < 1 || no > pages_) return false;
        buf.resize(pageSize_);
        if (fseek(f_, (long)((int64_t)(no - 1) * pageSize_), SEEK_SET) != 0) return false;
        return fread(buf.data(), 1, pageSize_, f_) == pageSize_;
    }

    // One b-tree cell's payload, overflow pages followed. `p` points into `pg`; payload starts at `at`.
    bool payload(const std::vector<uint8_t>& pg, size_t at, uint64_t total, bool indexTree, std::vector<uint8_t>& out) {
        out.clear();
        if (total > (1u << 28)) return false;
        const uint64_t U = usable_;
        const uint64_t X = indexTree ? ((U - 12) * 64 / 255) - 23 : U - 35;
        uint64_t local = total;
        if (total > X) {
            const uint64_t M = ((U - 12) * 32 / 255) - 23;
            const uint64_t K = M + ((total - M) % (U - 4));
            local = K <= X ? K : M;
        }
        if (at + local + (total > X ? 4 : 0) > pg.size()) return false;
        out.assign(pg.begin() + at, pg.begin() + at + local);
        if (total > X) {
            uint32_t next = be32(&pg[at + local]);
            std::vector<uint8_t> ov;
            int hops = 0;
            while (out.size() < total) {
                if (next == 0 || ++hops > 1000000 || !page(next, ov)) return false;
                const size_t take = (size_t)std::min<uint64_t>(total - out.size(), U - 4);
                out.insert(out.end(), ov.begin() + 4, ov.begin() + 4 + take);
                next = be32(ov.data());
            }
        }
        return true;
    }

    // A record -> its columns (blobs only copied when wantBlob)
    static bool record(const std::vector<uint8_t>& r, std::vector<Col>& cols, bool wantBlob = true) {
        cols.clear();
        const uint8_t* p = r.data(); const uint8_t* end = p + r.size();
        uint64_t hs;
        int n = varint(p, end, hs);
        if (!n || hs > r.size() || hs < (uint64_t)n) return false;
        const uint8_t* h = p + n; const uint8_t* he = p + hs;
        const uint8_t* d = he;
        while (h < he) {
            uint64_t t;
            const int m = varint(h, he, t);
            if (!m) return false;
            h += m;
            Col c;
            size_t len = 0;
            if (t == 0) { c.isNull = true; }
            else if (t >= 1 && t <= 6) {
                static const int sz[] = {0, 1, 2, 3, 4, 6, 8};
                len = sz[t];
                if (d + len > end) return false;
                int64_t v = (int8_t)d[0];
                for (size_t k = 1; k < len; k++) v = (v << 8) | d[k];
                c.isNull = false; c.isInt = true; c.i = v;
            } else if (t == 7) { len = 8; if (d + len > end) return false; c.isNull = false; }
            else if (t == 8) { c.isNull = false; c.isInt = true; c.i = 0; }
            else if (t == 9) { c.isNull = false; c.isInt = true; c.i = 1; }
            else if (t >= 12) {
                len = (size_t)((t - 12) / 2);
                if (d + len > end) return false;
                c.isNull = false;
                if (t & 1) c.s.assign((const char*)d, len);
                else { c.isBlob = true; if (wantBlob) c.b.assign(d, d + len); }
            } else return false;
            d += len;
            cols.push_back(std::move(c));
        }
        return true;
    }

    // Visit every row of a table b-tree (rowid, record); stops when fn returns false. Depth limited against loops.
    using RowFn = std::function<bool(int64_t, const std::vector<uint8_t>&)>;
    bool walkTable(uint32_t root, const RowFn& fn, int depth = 0) {
        if (depth > 20) return false;
        std::vector<uint8_t> pg;
        if (!page(root, pg)) return false;
        const size_t base = root == 1 ? 100 : 0;
        const uint8_t type = pg[base];
        const unsigned cells = be16(&pg[base + 3]);
        if (type == 13) {
            for (unsigned i = 0; i < cells; i++) {
                const size_t ptrAt = base + 8 + 2 * i;
                if (ptrAt + 2 > pg.size()) return false;
                size_t o = be16(&pg[ptrAt]);
                if (o >= pg.size()) return false;
                uint64_t total, rowid;
                int a = varint(&pg[o], pg.data() + pg.size(), total); if (!a) return false;
                int b = varint(&pg[o + a], pg.data() + pg.size(), rowid); if (!b) return false;
                std::vector<uint8_t> rec;
                if (!payload(pg, o + a + b, total, false, rec)) return false;
                if (!fn((int64_t)rowid, rec)) return true;
            }
            return true;
        }
        if (type == 5) {
            for (unsigned i = 0; i <= cells; i++) {
                uint32_t child;
                if (i < cells) {
                    const size_t ptrAt = base + 12 + 2 * i;
                    if (ptrAt + 2 > pg.size()) return false;
                    const size_t o = be16(&pg[ptrAt]);
                    if (o + 4 > pg.size()) return false;
                    child = be32(&pg[o]);
                } else child = be32(&pg[base + 8]);
                bool stop = false;
                if (!walkTable(child, [&](int64_t id, const std::vector<uint8_t>& r) { if (!fn(id, r)) { stop = true; return false; } return true; }, depth + 1)) return false;
                if (stop) return true;
            }
            return true;
        }
        return false;
    }

    bool readSchema() {
        std::vector<std::string> sqls[2];   // unused marker to keep the lambda simple
        (void)sqls;
        std::string tilesSql;
        struct Idx { uint32_t root; std::string name, sql; };
        std::vector<Idx> idx;
        uint32_t metaRoot = 0;
        const bool ok = walkTable(1, [&](int64_t, const std::vector<uint8_t>& r) {
            std::vector<Col> c;
            if (!record(r, c) || c.size() < 5) return true;
            const std::string& type = c[0].s; const std::string& name = c[1].s; const std::string& tbl = c[2].s;
            const uint32_t root = (uint32_t)c[3].i;
            if (type == "table" && name == "tiles") { tilesRoot_ = root; tilesSql = c[4].s; }
            else if (type == "table" && name == "metadata") metaRoot = root;
            else if (type == "index" && tbl == "tiles") idx.push_back({root, name, c[4].s});
            return true;
        });
        if (!ok || !tilesRoot_) return false;
        auto squash = [](std::string s) { std::string o; for (char ch : s) if (ch != ' ' && ch != '\n' && ch != '\t' && ch != '"' && ch != '`' && ch != '[' && ch != ']') o += (char)tolower((unsigned char)ch); return o; };
        // The column order of the table must be the standard one for the row decoding below
        const std::string ts = squash(tilesSql);
        const size_t lp = ts.find('(');
        if (lp == std::string::npos || ts.compare(lp + 1, 38, "zoom_level,tile_column,tile_row,tile_d") != 0) {
            // allow "zoom_level integer, tile_column integer, ..." forms: only the names in order
            size_t a = ts.find("zoom_level"), b = ts.find("tile_column"), c2 = ts.find("tile_row"), d = ts.find("tile_data");
            if (a == std::string::npos || !(a < b && b < c2 && c2 < d)) return false;
        }
        for (const Idx& i : idx) {
            const std::string s = squash(i.sql);
            if (s.find("(zoom_level,tile_column,tile_row)") != std::string::npos) { indexRoot_ = i.root; break; }
            if (i.sql.empty() && ts.find("(zoom_level,tile_column,tile_row)") != std::string::npos) { indexRoot_ = i.root; break; }   // autoindex of a PRIMARY KEY/UNIQUE
        }
        if (metaRoot) {
            walkTable(metaRoot, [&](int64_t, const std::vector<uint8_t>& r) {
                std::vector<Col> c;
                if (record(r, c) && c.size() >= 2 && !c[0].isNull) meta_.push_back({c[0].s, c[1].isInt ? std::to_string(c[1].i) : c[1].s});
                return meta_.size() < 200;
            });
        }
        return true;
    }

    static int cmp3(int64_t a0, int64_t a1, int64_t a2, int64_t b0, int64_t b1, int64_t b2) {
        if (a0 != b0) return a0 < b0 ? -1 : 1;
        if (a1 != b1) return a1 < b1 ? -1 : 1;
        if (a2 != b2) return a2 < b2 ? -1 : 1;
        return 0;
    }

    // The index b-tree: entries are (zoom_level, tile_column, tile_row, rowid)
    bool indexFind(int64_t z, int64_t x, int64_t row, int64_t& rowid) {
        uint32_t no = indexRoot_;
        for (int depth = 0; depth < 20; depth++) {
            std::vector<uint8_t> pg;
            if (!page(no, pg)) return false;
            const uint8_t type = pg[0];
            const unsigned cells = be16(&pg[3]);
            const bool leaf = type == 10;
            if (type != 10 && type != 2) return false;
            const size_t hdr = leaf ? 8 : 12;
            uint32_t next = leaf ? 0 : be32(&pg[8]);   // right-most child unless a cell says otherwise
            // binary search over the cells
            int lo = 0, hi = (int)cells - 1;
            bool descended = false;
            while (lo <= hi) {
                const int mid = (lo + hi) / 2;
                const size_t ptrAt = hdr + 2 * (size_t)mid;
                if (ptrAt + 2 > pg.size()) return false;
                size_t o = be16(&pg[ptrAt]);
                uint32_t child = 0;
                if (!leaf) { if (o + 4 > pg.size()) return false; child = be32(&pg[o]); o += 4; }
                uint64_t total;
                const int a = varint(&pg[o], pg.data() + pg.size(), total);
                if (!a) return false;
                std::vector<uint8_t> rec;
                if (!payload(pg, o + a, total, true, rec)) return false;
                std::vector<Col> c;
                if (!record(rec, c, false) || c.size() < 4 || !c[0].isInt || !c[1].isInt || !c[2].isInt || !c[3].isInt) return false;
                const int k = cmp3(z, x, row, c[0].i, c[1].i, c[2].i);
                if (k == 0) { rowid = c[3].i; return true; }   // interior cells carry entries too
                if (k < 0) { hi = mid - 1; next = child; }
                else lo = mid + 1;
            }
            if (leaf) return false;
            // the child to enter: the left child of the first cell greater than the key, else the right-most
            if (lo < (int)cells) {
                const size_t o = be16(&pg[hdr + 2 * (size_t)lo]);
                if (o + 4 > pg.size()) return false;
                next = be32(&pg[o]);
            } else next = be32(&pg[8]);
            descended = true;
            (void)descended;
            no = next;
        }
        return false;
    }

    bool rowToTile(const std::vector<uint8_t>& rec, std::vector<uint8_t>& out) {
        std::vector<Col> c;
        if (!record(rec, c) || c.size() < 4 || !c[3].isBlob) return false;
        out = std::move(c[3].b);
        return !out.empty();
    }

    // The table b-tree by rowid
    bool tableRowTile(int64_t rowid, std::vector<uint8_t>& out) {
        uint32_t no = tilesRoot_;
        for (int depth = 0; depth < 20; depth++) {
            std::vector<uint8_t> pg;
            if (!page(no, pg)) return false;
            const uint8_t type = pg[0];
            const unsigned cells = be16(&pg[3]);
            if (type == 5) {
                uint32_t next = be32(&pg[8]);
                for (unsigned i = 0; i < cells; i++) {
                    const size_t ptrAt = 12 + 2 * (size_t)i;
                    if (ptrAt + 2 > pg.size()) return false;
                    const size_t o = be16(&pg[ptrAt]);
                    if (o + 5 > pg.size()) return false;
                    uint64_t key;
                    if (!varint(&pg[o + 4], pg.data() + pg.size(), key)) return false;
                    if ((int64_t)key >= rowid) { next = be32(&pg[o]); break; }
                }
                no = next;
                continue;
            }
            if (type != 13) return false;
            int lo = 0, hi = (int)cells - 1;
            while (lo <= hi) {
                const int mid = (lo + hi) / 2;
                const size_t ptrAt = 8 + 2 * (size_t)mid;
                if (ptrAt + 2 > pg.size()) return false;
                const size_t o = be16(&pg[ptrAt]);
                if (o >= pg.size()) return false;
                uint64_t total, id;
                const int a = varint(&pg[o], pg.data() + pg.size(), total); if (!a) return false;
                const int b = varint(&pg[o + a], pg.data() + pg.size(), id); if (!b) return false;
                if ((int64_t)id == rowid) {
                    std::vector<uint8_t> rec;
                    return payload(pg, o + a + b, total, false, rec) && rowToTile(rec, out);
                }
                if ((int64_t)id < rowid) lo = mid + 1; else hi = mid - 1;
            }
            return false;
        }
        return false;
    }

    bool scanTile(int64_t z, int64_t x, int64_t row, std::vector<uint8_t>& out) {
        bool found = false;
        walkTable(tilesRoot_, [&](int64_t, const std::vector<uint8_t>& r) {
            std::vector<Col> c;
            if (!record(r, c, false) || c.size() < 4 || !c[0].isInt || !c[1].isInt || !c[2].isInt) return true;
            if (c[0].i == z && c[1].i == x && c[2].i == row) { found = rowToTile(r, out); return false; }
            return true;
        });
        return found;
    }
};

}
