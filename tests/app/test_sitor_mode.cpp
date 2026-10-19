// The SITOR mode is registered: a row in the mode table, a UI case in modeui.cpp, the family count covers it, and the
// panel carries the unverified-table warning. Reads the sources (DECT2_SOURCE_DIR), as test_credits does.
#include <fstream>
#include <sstream>
#include <string>
#include "../common/test_util.h"

static std::string readSource(const char* rel) {
    std::ifstream f(std::string(DECT2_SOURCE_DIR) + "/" + rel, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    const std::string table = readSource("app/ui/mode_table.cpp");
    const std::string modeui = readSource("app/panels/modeui.cpp");
    const std::string app = readSource("app/app.h");
    const std::string panel = readSource("app/hfdig/hfdig_sitor_ui.cpp");
    CHECK(table.find("{119, \"SITOR (WIP, table unverified)\"") != std::string::npos, "SITOR row (family 119) missing from the mode table");
    CHECK(modeui.find("case 119: return &kSitorUi;") != std::string::npos, "family 119 has no UI in modeui.cpp");
    CHECK(app.find("kNumFamilies = 120;") != std::string::npos, "kNumFamilies does not cover family 119");
    CHECK(panel.find("Character table not yet checked against ITU-R M.476; text may be wrong.") != std::string::npos, "SITOR panel lacks the table warning");
    return testSummary("sitor_mode");
}
