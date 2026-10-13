#include "../app/results_export.h"
#include <clocale>
#include <cstdio>
#include <cstdlib>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    setlocale(LC_ALL, "");
    resexp::Table t;
    t.cols = {"name", "freq_hz", "note"};
    t.add({"plain", resexp::num(522000000.0, 0), "ok"});
    t.add({"a,b", resexp::num(88.1234, 2), "say \"hi\""});
    t.add({"line1\nline2", "-3.5", "Zürich \xE2\x82\xAC"});
    const std::string csv = resexp::toCsv(t);
    CHECK(csv.rfind("name,freq_hz,note\r\n", 0) == 0);
    CHECK(csv.find("plain,522000000,ok\r\n") != std::string::npos);
    CHECK(csv.find("\"a,b\",88.12,\"say \"\"hi\"\"\"\r\n") != std::string::npos);
    CHECK(csv.find("\"line1\nline2\",-3.5,Zürich \xE2\x82\xAC") != std::string::npos);
    const std::string js = resexp::toJson(t);
    CHECK(js.find("\"freq_hz\": 522000000") != std::string::npos);
    CHECK(js.find("\"name\": \"a,b\"") != std::string::npos);
    CHECK(js.find("say \\\"hi\\\"") != std::string::npos);
    CHECK(js.find("line1\\nline2") != std::string::npos);
    CHECK(js.find("Zürich \xE2\x82\xAC") != std::string::npos);
    CHECK(resexp::num(1.5, 3) == "1.5");
    CHECK(resexp::num(-0.0001, 2) == "0");
    CHECK(resexp::num(100.0, 3) == "100");
    CHECK(resexp::stamp().size() == 13);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails;
}
