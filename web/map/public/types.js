// types.js - pure mapping from ICAO type code / ADS-B emitter category / AIS ship type to an icon shape name.
// No DOM; covered by web/map/test/types.test.js. Shape names are drawn in shapes.js.

const set = (s) => new Set(s.split(/\s+/).filter(Boolean));

const QUAD = set("A388 A38F A124 A225 A346 A345 A343 A342 B741 B742 B743 B744 B748 B74D B74R B74S B703 B720 IL76 IL96 C5M C5 BLCF");
const WIDE = set(`A306 A30B A310 A332 A333 A337 A338 A339 A359 A35K A332 A343X B762 B763 B764 B772 B773 B77L B77W B788 B789 B78X B703X
  DC10 MD11 L101 IL86 A3ST K35R KC10 E6 E3TF E3CF`);
const NARROW = set(`A318 A319 A320 A321 A19N A20N A21N A220 BCS1 BCS3 B731 B732 B733 B734 B735 B736 B737 B738 B739 B37M B38M B39M B3XM
  B752 B753 B712 B722 B721 MD80 MD81 MD82 MD83 MD87 MD88 MD90 DC91 DC92 DC93 DC94 DC95 C919 SU95 T154 T134 TU34 YK42 BE40`);
const REGIONAL = set(`E170 E175 E190 E195 E75L E75S E290 E295 E135 E145 E45X E35L E50P CRJ1 CRJ2 CRJ7 CRJ9 CRJX F70 F100 F28 RJ70 RJ85 RJ1H
  B461 B462 B463 AR85 CL60 CL30 GLEX GL5T GL7T GLF4 GLF5 GLF6 GLF3 FA7X FA8X FA50 FA20 C25A C25B C25C C510 C525 C550 C560 C56X C680 C700 C750
  LJ35 LJ45 LJ60 LJ75 H25B H25C HDJT E55P E50P PRM1 SF50 EA50 ASTR WW24 FA10 FA6X`);
const TURBOPROP = set(`AT43 AT44 AT45 AT46 AT72 AT73 AT75 AT76 DH8A DH8B DH8C DH8D DHC6 DHC7 DHC5 SF34 SB20 B190 B350 BE20 BE30 BE99 BE9L BE9T
  JS31 JS32 JS41 E120 PC12 PC6T C208 C212 CN35 C295 D228 D328 F27 F50 F60 SW3 SW4 AN24 AN26 AN32 L410 L188 TBM7 TBM8 TBM9 P180 P46T M600 PAY1
  PAY2 PAY3 PAY4 DC3 DC3T C130 C30J A400 SH36 SH33 ATP BN2P DHC2 DHC3 NOMA B36T AC90 AC95 MU2 SF3`);
const HELI = set(`R22 R44 R66 EC20 EC25 EC30 EC35 EC45 EC55 EC75 EC13 H125 H130 H135 H145 H160 H175 H225 AS32 AS33 AS35 AS50 AS55 AS65 A109 A119 A139
  A169 A189 AW09 B06 B06T B105 B204 B205 B212 B214 B222 B407 B412 B427 B429 B430 B505 S55 S58 S61 S64 S76 S92 S70 H60 UH1 UH60 H47 H53 H64 H500
  MD52 MD60 MD90X EXPL NH90 NH9 KA32 KA27 MI8 MI17 MI24 MI26 MI2 MI34 LYNX PUMA SCOU GAZL W3 EN28 EN48 CABR BK17 HUCO TIGR AH64 CH47 H46`);
const MILITARY = set(`F16 F15 F18 FA18 F14 F22 F35 F117 F5 EUFI RFAL TORN GRIF JAS39 M346 MIG2 MIG3 MG29 SU27 SU30 SU34 SU35 SU57 A10 AV8B
  B1 B2 B52 B1B T38 T6 TEX2 HAWK PC21 PC9 PC7 TUCA T37 T45 T33 F4 F104 MIR2 M2K A4 A7 EA6B EA18 AJET MB33 L39 L159 YK30 A29 EMB314 U2 SR71 RQ4
  MQ9 MQ1 P3 P8 P8A E2 E3 E8 E7 C2 C17 KC2 K35T KC46 A337M C27J CASA H60M`);
const GLIDER = set("GLID ASK2 ASK21 ASW2 ASW20 ASW27 ASG29 LS4 LS6 LS8 DG10 DG80 DG50 DISCUS DUOD JANU NIMB VENT");
const BALLOON = set("BALL GBAL HAB");
const DRONE = set("UAV DRON");
const LIGHT = set(`C150 C152 C162 C170 C172 C175 C177 C180 C182 C185 C188 C195 C206 C207 C210 C310 C337 C340 C402 C414 C421 PA18 PA22 PA24 PA25
  PA28 PA32 PA34 PA38 PA44 PA46 PA31 SR20 SR22 S22T BE23 BE24 BE33 BE35 BE36 BE55 BE58 BE60 BE76 DA20 DA40 DA42 DA62 DR40 TB10 TB20 TB21
  M20P M20T M20J G115 AA5 AT3 AEST RV6 RV7 RV8 RV9 RV10 EVOT J3 PIVI Y12 GA8 COZY CH7A CH7B ULAC AC11 AA1 AAT3 P28A P28R P28B P32R SIRA`);

/** Pick a family for an ICAO type code, or "" when unknown. */
export function familyOfType(code) {
  const c = String(code || "").toUpperCase().trim();
  if (!c) return "";
  if (MILITARY.has(c)) return "military";
  if (HELI.has(c)) return "heli";
  if (GLIDER.has(c)) return "glider";
  if (BALLOON.has(c)) return "balloon";
  if (DRONE.has(c)) return "drone";
  if (QUAD.has(c)) return "quad";
  if (WIDE.has(c)) return "wide";
  if (NARROW.has(c)) return "narrow";
  if (REGIONAL.has(c)) return "regional";
  if (TURBOPROP.has(c)) return "turboprop";
  if (LIGHT.has(c)) return "light";
  return "";
}

/** US military ICAO hex block (AE0000-AFFFFF). */
export function isMilitaryHex(hex) {
  const n = parseInt(String(hex || ""), 16);
  return Number.isFinite(n) && n >= 0xAE0000 && n <= 0xAFFFFF;
}

/** Family from an ADS-B emitter category such as "A3" (set A = aircraft, B = other), "" when unknown. */
export function familyOfCategory(cat) {
  const m = String(cat || "").toUpperCase().match(/\b([AB])([0-7])\b/);
  if (!m) return "";
  const k = m[1] + m[2];
  switch (k) {
    case "A1": return "light";
    case "A2": return "regional";
    case "A3": return "narrow";
    case "A4": return "narrow"; // high-vortex large (B757 class)
    case "A5": return "wide";
    case "A6": return "military"; // high-performance
    case "A7": return "heli";
    case "B1": return "glider";
    case "B2": return "balloon";
    case "B3": return "light"; // parachutist: nearest generic
    case "B4": return "light"; // ultralight
    case "B6": return "drone";
    default: return "";
  }
}

/** Aircraft shape: type code first, then military hex, then emitter category, then the generic airliner. */
export function aircraftShape({ icaoType, cat, hex } = {}) {
  return familyOfType(icaoType) || (isMilitaryHex(hex) ? "military" : "") || familyOfCategory(cat) || "narrow";
}

/** Ship shape from AIS ship-type text or numeric code (30 fishing, 36/37 sail, 52 tug, 6x passenger, 7x cargo, 8x tanker). */
export function vesselShape({ cat, extra, sub } = {}) {
  const txt = [cat, extra, sub].filter(Boolean).join(" ").toLowerCase();
  const num = txt.match(/\b(\d{2})\b/);
  const code = num ? Number(num[1]) : null;
  if (/tanker/.test(txt) || (code >= 80 && code <= 89)) return "ship_tanker";
  if (/cargo|container|bulk|freight/.test(txt) || (code >= 70 && code <= 79)) return "ship_cargo";
  if (/passenger|ferry|cruise/.test(txt) || (code >= 60 && code <= 69)) return "ship_passenger";
  if (/fishing/.test(txt) || code === 30) return "ship_fishing";
  if (/sail|yacht|pleasure/.test(txt) || code === 36 || code === 37) return "ship_sailing";
  if (/\btug\b|towing|tow\b/.test(txt) || code === 52 || code === 31 || code === 32) return "ship_tug";
  return "ship_other";
}

/** APRS shape from the symbol/comment text when the data has it: car, house, digi, else generic. */
export function aprsShape({ cat, extra, sub, label } = {}) {
  const txt = [cat, extra, sub].filter(Boolean).join(" ").toLowerCase();
  if (/digi|relay|wide\d|igate|repeater|node/.test(txt)) return "aprs_digi";
  if (/house|home|qth|fixed|weather|wx/.test(txt)) return "aprs_house";
  if (/car|vehicle|mobile|truck|van|bike|auto|jeep/.test(txt)) return "aprs_car";
  return "aprs";
}

/** Shape name for any track ({kind, icaoType?, cat, sub, extra, label, hex?}). */
export function iconShape(t) {
  switch (t.kind) {
    case "aircraft": return aircraftShape({ icaoType: t.icaoType || (t.meta && t.meta.icaoType), cat: t.cat, hex: t.hex });
    case "flarm": return familyOfCategory(t.cat) || (/heli/i.test(t.cat || "") ? "heli" : /balloon/i.test(t.cat || "") ? "balloon" : /drone|uav/i.test(t.cat || "") ? "drone" : /(tow|plane|jet|aircraft|motor)/i.test(t.cat || "") ? "light" : "glider");
    case "vessel": return vesselShape(t);
    case "aprs": return aprsShape(t);
    case "sonde": return "sonde";
    case "sat":
    case "gnss": return "sat";
    case "drone": return "drone";
    case "ham": return "ham";
    case "buoy": return "buoy";
    case "dsc": return "dsc";
    case "station": return "station";
    default: return "station";
  }
}

/** Human names for the card illustration caption. */
export const FAMILY_NAME = {
  narrow: "Narrow-body airliner", wide: "Wide-body airliner", quad: "Four-engine airliner", regional: "Regional / business jet",
  turboprop: "Turboprop", light: "Light aircraft", heli: "Helicopter", military: "Military / high-performance",
  glider: "Glider", balloon: "Balloon", drone: "Drone",
};
