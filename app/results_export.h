// Shared "Save results..." export: columns + rows -> CSV or JSON. No GUI includes here, so tests can link it.
#pragma once
#include <string>
#include <vector>

namespace resexp {

struct Table {
    std::vector<std::string> cols;
    std::vector<std::vector<std::string>> rows;
    void add(std::vector<std::string> r) { rows.push_back(std::move(r)); }
};

// locale-independent number text with '.', trimmed to `dec` decimals
std::string num(double v, int dec = 3);
std::string toCsv(const Table& t);    // UTF-8 header row, RFC 4180 quoting, CRLF
std::string toJson(const Table& t);   // array of objects keyed by column name; numeric-looking cells stay numbers
std::string stamp();                  // YYYYmmdd-HHMM, local time

// GUI half (results_export_ui.cpp): floppy-icon "Save results..." button; kind = "lte", "dvb"...
// Asks for a file (.csv default, .json by file name), writes it, puts a status line in `note`.
bool saveButton(const Table& t, const char* kind, std::string& note, const char* id = "saveres");

}  // namespace resexp
