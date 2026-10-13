// GUI half of the results export (dialog + button); the pure half is results_export.cpp.
#include "results_export.h"
#include "app.h"
#include "icons.h"
#include <fstream>

namespace resexp {

bool saveButton(const Table& t, const char* kind, std::string& note, const char* id) {
    ImGui::PushID(id);
    bool hit = iconFlat(Ic::Save, "Save results as CSV (or JSON: give the file a .json name)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save results...")) hit = true;
    ImGui::PopID();
    if (!hit) return false;
    const std::string nm = std::string("scan-") + kind + "-" + stamp() + ".csv";
    const std::string path = saveFileDialog(nm.c_str());
    if (path.empty()) return true;
    const bool json = path.size() >= 5 && path.compare(path.size() - 5, 5, ".json") == 0;
    std::ofstream f(path, std::ios::binary);
    f << (json ? toJson(t) : toCsv(t));
    f.flush();
    note = f ? "saved " + path : "could not write " + path;
    return true;
}

}  // namespace resexp
