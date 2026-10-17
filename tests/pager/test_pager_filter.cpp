// Pagers: the "Decoded only" predicate hides tone-only pages and pages with no text, for POCSAG and FLEX alike.
#include "../../app/pager/pager_filter.h"
#include <cstdio>
using namespace dect2;
#include "../common/test_util.h"

static PagerMessage msg(bool flex, int type, const char* text) {
    PagerMessage m; m.flex = flex; m.type = type; m.text = text; return m;
}

int main() {
    CHECK(!pagerIsDecoded(msg(false, kPagerTone, "")), "POCSAG tone hidden");
    CHECK(!pagerIsDecoded(msg(false, kPagerTone, "ignored")), "tone type wins over text");
    CHECK(pagerIsDecoded(msg(false, kPagerNumeric, "0123")), "numeric kept");
    CHECK(pagerIsDecoded(msg(false, kPagerAlpha, "FIRE CALL")), "alpha kept");
    CHECK(!pagerIsDecoded(msg(false, kPagerAlpha, "")), "empty alpha hidden");
    CHECK(!pagerIsDecoded(msg(false, kPagerAlpha, " \r\n")), "blank alpha hidden");
    CHECK(pagerIsDecoded(msg(true, kPagerAlpha, "FLEX text")), "FLEX text kept");
    CHECK(!pagerIsDecoded(msg(true, kPagerAlpha, "")), "FLEX empty hidden");
    CHECK(pagerIsDecoded(msg(false, kPagerAlpha, "secure, not shown")), "secure page kept");
    return testSummary("pager_filter");
}
