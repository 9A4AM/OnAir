// Navigation beacon list and the position from two VOR radials.
//
// No station table is built in (there are thousands). The user imports a CSV:
//  - OurAirports navaids (public domain): https://davidmegginson.github.io/ourairports-data/navaids.csv
//    columns ident, name, type, frequency_khz, latitude_deg, longitude_deg, magnetic_variation_deg / slaved_variation_deg, iso_country
//  - OpenAIP (CC BY-NC 4.0, so never bundled): an export of the navaids of a country from https://www.openaip.net, as CSV with columns
//    such as identifier / ident, name, type, frequency (MHz or kHz), latitude / lat, longitude / lon, magneticDeclination / declination
// Any CSV with a header naming such columns is read; only beacons on 74 - 118 or 328 - 336 MHz are kept (VOR, VOR-DME, VORTAC, DVOR,
// ILS / localizer, glide path, markers), NDBs and DMEs alone are dropped.
#pragma once
#include <string>
#include <vector>

namespace dect2 {

struct NavStation {
    std::string ident, name, type, country;
    double freqMhz = 0;
    double lat = 0, lon = 0;
    double magVarDeg = 0;        // east positive: true bearing = magnetic + magVarDeg (the VOR's own, slaved, variation when known)
};

// Parses the CSV text; appends to out. Returns the number of stations read; err says why none.
int navParseCsv(const std::string& text, std::vector<NavStation>& out, std::string& err);
// The station with this identification, preferring one on this frequency (0: any); nullptr when none or ambiguous without a frequency
const NavStation* navMatch(const std::vector<NavStation>& db, const std::string& ident, double freqMhz);

// The position from two radials (true bearings FROM the stations, degrees) and their standard deviations (degrees).
struct NavFix {
    bool ok = false;
    std::string why;             // when not ok: parallel radials, behind a station ...
    double lat = 0, lon = 0;
    double sigMajorM = 0, sigMinorM = 0;   // the 1-sigma error ellipse's semi-axes, metres (x 2.45 for 95 %)
    double majorAzDeg = 0;       // the major axis's direction, degrees from true north
    double crossDeg = 0;         // the angle the radials cross at (90 is best)
    double dist1M = 0, dist2M = 0;   // from each station
};
NavFix navFix(double lat1, double lon1, double brg1, double sig1, double lat2, double lon2, double brg2, double sig2);

// The point dist metres from (lat, lon) along the true bearing brg (great circle), for drawing radials
void navDestination(double lat, double lon, double brgDeg, double distM, double& outLat, double& outLon);
double navDistanceM(double lat1, double lon1, double lat2, double lon2);
double navBearingDeg(double lat1, double lon1, double lat2, double lon2);   // initial true bearing from 1 to 2

} // namespace dect2
