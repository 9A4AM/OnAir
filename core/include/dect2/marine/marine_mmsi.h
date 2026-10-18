// MMSI formats (ITU-R M.585): what kind of station a number names and which country the MID belongs to, plus an optional local name table.
#pragma once
#include <string>
#include <unordered_map>

namespace dect2 {
namespace marine {

enum class MmsiKind { Unknown, Ship, CoastStation, Group, SarAircraft, Handheld, CraftAssociated, AisSart, Mob, EpirbAis, AtoN, Special };

struct MmsiInfo {
    MmsiKind kind = MmsiKind::Unknown;
    int mid = 0;                 // maritime identification digits, 0 when the number has none
    std::string country;         // "" when the MID is not allocated
    bool partial = false;        // the text holds '~' for digits that could not be read
};

// Digits only (9 of them, '~' allowed for unreadable ones). 00MIDxxxx coast, 0MIDxxxxx group, MIDxxxxxx ship, 111MIDxxx SAR aircraft,
// 8MIDxxxxx handheld VHF with DSC, 98MIDxxxx craft associated with a parent ship, 970/972/974 xxxxxx AIS-SART/MOB/EPIRB-AIS, 99MIDxxxx AtoN.
MmsiInfo mmsiInfo(const std::string& mmsi);
const char* mmsiKindName(MmsiKind k);
const char* midCountry(int mid);        // "" for an unallocated MID

// "mmsi<TAB>name" per line (also comma or semicolon separated, '#' comments). Later lines replace earlier ones.
class MmsiNames {
public:
    int load(const std::string& text);                 // returns the number of entries read
    std::string name(const std::string& mmsi) const;   // "" when not listed
    size_t size() const { return m_.size(); }
private:
    std::unordered_map<std::string, std::string> m_;
};

} // namespace marine
} // namespace dect2
