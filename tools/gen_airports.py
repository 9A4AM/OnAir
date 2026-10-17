#!/usr/bin/env python3
"""Build data/airports/airports.tsv, the airport table of the airband mode, from the OurAirports CSV files (public domain).

  python3 -I tools/gen_airports.py --airports AIRPORTS.csv --frequencies AIRPORT-FREQUENCIES.csv [--out data/airports/airports.tsv]

The inputs are https://davidmegginson.github.io/ourairports-data/airports.csv and .../airport-frequencies.csv; put them in an empty folder
of their own (never run anything from there). Kept: every large and medium airport, every small airport or seaplane base with scheduled
service or at least one frequency. Dropped: closed airports, heliports, balloon ports and airports without a position.
Output, one text file, tab separated:
  # comment lines
  A  ident  iata  name  city  country  lat  lon  elev_ft  kind(L large, M medium, S small, W seaplane)  scheduled(0/1)
  F  type  kHz  description          (belongs to the airport line above; the frequency in whole kHz: 118505 is the 8.33 kHz channel "118.505")
Frequencies are normalised to integer kHz. A frequency in the airband (118 - 137 MHz) that is on neither the 25 kHz nor the 8.33 kHz
raster is snapped to the nearest 25 kHz channel when it is within 5 kHz of one (old 20 kHz and mis-typed entries), otherwise dropped.
"""
import argparse, csv, hashlib, os, sys

KIND = {"large_airport": "L", "medium_airport": "M", "small_airport": "S", "seaplane_base": "W"}
TYPE_FIX = {"UNIC": "UNICOM", "Info": "INFO", "Radio": "RADIO", "A/D": "A/D"}


def norm_khz(mhz):
    """MHz as typed -> integer kHz on the raster (airband) or whole kHz elsewhere; None when unusable."""
    try:
        v = float(mhz)
    except ValueError:
        return None
    if not (v > 0):
        return None
    k = round(v * 1000)
    if 118000 <= k <= 137000:
        r = k % 25
        if r in (0, 5, 10, 15):
            return k          # a 25 kHz channel (r == 0) or an 8.33 kHz channel name (118.505 ...)
        base = k - r
        if r <= 5:
            return base       # snap down
        if r >= 20:
            return base + 25  # snap up
        return None
    if 1 <= k < 100000 or k > 3000000:
        return None
    return k


def clean(s):
    return " ".join(s.replace("\t", " ").replace("\r", " ").replace("\n", " ").split())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--airports", required=True)
    ap.add_argument("--frequencies", required=True)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data/airports/airports.tsv"))
    a = ap.parse_args()
    sha = {}
    for p in (a.airports, a.frequencies):
        sha[p] = hashlib.sha256(open(p, "rb").read()).hexdigest()
    freqs = {}
    for r in csv.DictReader(open(a.frequencies, encoding="utf-8", newline="")):
        k = norm_khz(r["frequency_mhz"])
        if k is None:
            continue
        t = clean(r["type"]).upper()
        t = TYPE_FIX.get(t, t) or "MISC"
        d = clean(r["description"])
        if d.upper() == t or d.upper() == r["type"].upper():
            d = ""
        freqs.setdefault(r["airport_ref"], {})[(k, t)] = d[:40]
    rows = []
    for r in csv.DictReader(open(a.airports, encoding="utf-8", newline="")):
        kind = KIND.get(r["type"])
        if not kind or not r["latitude_deg"] or not r["longitude_deg"]:
            continue
        f = freqs.get(r["id"], {})
        sched = r["scheduled_service"] == "yes"
        if kind in ("S", "W") and not f and not sched:
            continue
        try:
            lat, lon = float(r["latitude_deg"]), float(r["longitude_deg"])
        except ValueError:
            continue
        ident = clean(r["icao_code"] or r["ident"])
        elev = r["elevation_ft"].split(".")[0]
        rows.append((ident, clean(r["iata_code"]), clean(r["name"]), clean(r["municipality"]), r["iso_country"], "%.4f" % lat, "%.4f" % lon,
                     elev, kind, "1" if sched else "0", sorted(f.items())))
    rows.sort(key=lambda x: (x[8] != "L", x[8] != "M", x[0]))
    rows.sort(key=lambda x: x[0])
    nf = 0
    with open(a.out, "w", encoding="utf-8", newline="\n") as o:
        o.write("# Airports and their radio frequencies: built by tools/gen_airports.py from OurAirports (public domain, https://ourairports.com/data/).\n")
        o.write("# A ident iata name city country lat lon elev_ft kind(L M S W) scheduled / F type kHz description (belongs to the A line above)\n")
        o.write("# airports.csv sha256 %s\n# airport-frequencies.csv sha256 %s\n" % (sha[a.airports], sha[a.frequencies]))
        for ident, iata, name, city, cc, lat, lon, elev, kind, sched, f in rows:
            o.write("A\t" + "\t".join((ident, iata, name, city, cc, lat, lon, elev, kind, sched)) + "\n")
            for (k, t), d in sorted(f, key=lambda kv: (kv[0][1], kv[0][0])):
                o.write("F\t%s\t%d\t%s\n" % (t, k, d))
                nf += 1
    print("%d airports, %d frequencies, %d bytes" % (len(rows), nf, os.path.getsize(a.out)))


if __name__ == "__main__":
    sys.exit(main())
