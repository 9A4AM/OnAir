// Pagers: the "Decoded only" table filter. Pure predicate so a test can check it; the UI applies it to the message table, the Copy export and the side list.
#pragma once
#include "dect2/pager/pager_tel.h"

namespace dect2 {

// true when the page carries readable content: not a tone-only page and not one whose text came out empty (FLEX has no tone type; an empty text there is a failed decode).
inline bool pagerIsDecoded(const PagerMessage& m) {
    if (m.type == kPagerTone) return false;
    for (char c : m.text)
        if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n')) return true;
    return false;
}

}  // namespace dect2
