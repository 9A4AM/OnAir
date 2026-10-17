// The Credits table (app/panels/credits_data.h, generated from tools/credits_data.tsv): no empty field, every link is https, no duplicates.
#include "panels/credits_data.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

int main() {
    int fails = 0;
    std::set<std::string> seen;
    if (kCreditCount < 30) { std::printf("FAIL: only %d entries\n", kCreditCount); fails++; }
    for (int i = 0; i < kCreditCount; i++) {
        const CreditEntry& c = kCredits[i];
        const char* f[7] = {c.name, c.author, c.licence, c.where, c.how, c.url, c.note};
        const char* n[7] = {"name", "author", "licence", "where", "how", "url", "note"};
        for (int k = 0; k < 7; k++)
            if (!f[k] || !f[k][0]) { std::printf("FAIL: entry %d (%s): empty %s\n", i, c.name ? c.name : "?", n[k]); fails++; }
        if (c.url && std::strncmp(c.url, "https://", 8) != 0) { std::printf("FAIL: entry %d: link is not https\n", i); fails++; }
        if (c.name && !seen.insert(c.name).second) { std::printf("FAIL: duplicate %s\n", c.name); fails++; }
    }
    if (!fails) std::printf("credits: %d entries ok\n", kCreditCount);
    return fails ? 1 : 0;
}
