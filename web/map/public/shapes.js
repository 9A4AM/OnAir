// shapes.js - icon geometry as SVG path data in a -32..32 box, nose up. Pure data (no DOM).
// Each shape = { body: [path...], detail: [path...] (dark strokes), lines: [path...] (thin strokes), rotates: bool }.
// icons.js draws these on canvas for the map; card.js reuses them as inline SVG silhouettes.

/** Mirror a right-half polyline (nose to tail, x >= 0) into one closed symmetric path. */
function mirror(pts) {
  const right = pts.map(([x, y]) => x.toFixed(2) + "," + y.toFixed(2));
  const left = pts.slice().reverse().map(([x, y]) => (-x).toFixed(2) + "," + y.toFixed(2));
  return "M" + right.join("L") + "L" + left.join("L") + "Z";
}

/** Rectangle path centred at (cx, cy) with half sizes (hw, hh). */
const rect = (cx, cy, hw, hh) => `M${cx - hw},${cy - hh}H${cx + hw}V${cy + hh}H${cx - hw}Z`;
/** The same rectangle plus its mirror image about x = 0 (a pair of engines). */
const rect2 = (cx, cy, hw, hh) => rect(cx, cy, hw, hh) + rect(-cx, cy, hw, hh);

/** Swept-wing airliner planform: wing half span, wing sweep offset, fuselage half width, tailplane half span, engine paths. */
const airliner = (span, sweepY, fus, tail, engines) => ({
  rotates: true,
  body: [mirror([
    [0, -30], [fus * 0.7, -28.6], [fus, -24], [fus, -8],
    [span, sweepY], [span, sweepY + 3.2], [fus * 1.2, 2.5],
    [fus, 14], [tail, 25], [tail, 27.6], [fus * 0.7, 26], [0, 29.5],
  ])].concat(engines),
  detail: [],
});

export const SHAPES = {
  narrow: airliner(22, 3, 2.1, 9, [rect2(8.5, -0.5, 1.5, 3.2)]),
  wide: airliner(26.5, 5, 2.9, 11, [rect2(9.5, 0.5, 1.9, 3.6)]),
  quad: airliner(27.5, 5.5, 3.1, 11.5, [rect2(8.5, 0, 1.7, 3.6), rect2(15.5, 3, 1.6, 3.4)]),
  regional: {
    rotates: true,
    body: [mirror([[0, -29], [1.8, -27], [2, -22], [2.1, -8], [16, 2.5], [16, 5.2], [2.3, 3], [2.1, 18], [8.5, 23], [8.5, 25.6], [1.6, 25], [0, 28.5]]),
      rect2(4.3, 14, 1.8, 3.4)],
    detail: [],
  },
  turboprop: {
    rotates: true,
    body: [mirror([[0, -29], [1.8, -27], [2, -20], [2.1, -8], [22, -4], [22, 0.5], [2.3, 1.5], [2.1, 16], [9.5, 23.5], [9.5, 26], [1.6, 25], [0, 28.5]]),
      rect2(8, -6, 1.5, 4.2)],
    detail: [],
    lines: ["M-10.5,-12H-5.5M5.5,-12H10.5"],
  },
  light: {
    rotates: true,
    body: [mirror([[0, -26], [1.8, -24], [2, -14], [2.1, -8], [17, -7], [17, -2], [2.2, 0], [1.6, 16], [7.5, 22], [7.5, 25], [1, 24.5], [0, 27]])],
    detail: [],
    lines: ["M-6,-26H6"],
  },
  heli: {
    rotates: true,
    body: ["M0,-14C5.5,-14 7,-8 7,-3C7,3 4,8 1.5,9L1,26L3.6,28L-3.6,28L-1,26L-1.5,9C-4,8 -7,3 -7,-3C-7,-8 -5.5,-14 0,-14Z"],
    detail: ["M-4,-5H4"],
    lines: ["M-24,-20L24,16M24,-20L-24,16"],
  },
  military: {
    rotates: true,
    body: [mirror([[0, -30], [1.5, -24], [2.4, -12], [14.5, 8.5], [14.5, 11.5], [3.4, 11], [2.8, 18], [7.5, 23.5], [7.5, 26], [2, 25], [0, 28]])],
    detail: ["M0,-20V-12"],
  },
  glider: {
    rotates: true,
    body: [mirror([[0, -24], [1.3, -22], [1.5, -14], [30, -10], [30, -6.6], [1.5, -5], [1.1, 14], [8.5, 22], [8.5, 24.4], [1, 23], [0, 25]])],
    detail: [],
  },
  balloon: {
    rotates: false,
    body: ["M0,-30C14,-30 18,-16 14,-6C12,-1 8,3 4,6H-4C-8,3 -12,-1 -14,-6C-18,-16 -14,-30 0,-30Z", rect(0, 12, 3.6, 3.6)],
    detail: [],
    lines: ["M-4,6L-3.6,8.4M4,6L3.6,8.4M0,-30V6"],
  },
  drone: {
    rotates: true,
    body: [rect(0, 0, 4.4, 6.6),
      "M-9,-9m-7,0a7,7 0 1,0 14,0a7,7 0 1,0 -14,0M9,-9m-7,0a7,7 0 1,0 14,0a7,7 0 1,0 -14,0M-9,11m-7,0a7,7 0 1,0 14,0a7,7 0 1,0 -14,0M9,11m-7,0a7,7 0 1,0 14,0a7,7 0 1,0 -14,0"],
    detail: [],
    lines: ["M-6,-6L-9,-9M6,-6L9,-9M-6,7L-9,11M6,7L9,11"],
  },
  // ---- ships ----
  ship_other: {
    rotates: true,
    body: [mirror([[0, -30], [5, -20], [6.6, -4], [6.2, 22], [4.4, 28], [0, 28]])],
    detail: ["M-3.4,6H3.4V16H-3.4Z"],
  },
  ship_cargo: {
    rotates: true,
    body: [mirror([[0, -30], [5, -22], [6.6, -8], [6.4, 26], [4.6, 29.5], [0, 29.5]])],
    detail: ["M-4.2,-12H4.2V-3H-4.2ZM-4.2,-1H4.2V8H-4.2Z", "M-4.4,19H4.4V26H-4.4Z"],
  },
  ship_tanker: {
    rotates: true,
    body: [mirror([[0, -30], [4.2, -26], [6.4, -14], [6.6, 24], [5, 29.5], [0, 29.5]])],
    detail: ["M0,-20V8", "M-4.4,18H4.4V26H-4.4Z", "M-3,-12H3M-3,-4H3M-3,4H3"],
  },
  ship_passenger: {
    rotates: true,
    body: [mirror([[0, -30], [5.6, -22], [8, -6], [8, 22], [6, 29], [0, 29]])],
    detail: ["M-5.2,-8H5.2V22H-5.2Z", "M-5.2,-2H5.2M-5.2,5H5.2M-5.2,12H5.2"],
  },
  ship_fishing: {
    rotates: true,
    body: [mirror([[0, -26], [4.4, -16], [5.4, 4], [5, 20], [3.4, 25], [0, 25]])],
    detail: ["M-3.2,8H3.2V18H-3.2Z"],
    lines: ["M-12,-6L0,-1L12,-6M0,-1V8"],
  },
  ship_sailing: {
    rotates: true,
    body: [mirror([[0, -24], [3.6, -14], [4.4, 6], [4, 20], [2.4, 24], [0, 24]]), "M0,-16L8,6L0,6Z"],
    detail: ["M0,-16V6"],
  },
  ship_tug: {
    rotates: true,
    body: [mirror([[0, -18], [6.4, -12], [8, 4], [7, 18], [4, 21], [0, 21]])],
    detail: ["M-3.6,0H3.6V12H-3.6Z", "M0,-18V-12"],
  },
  // ---- other layers ----
  sonde: {
    rotates: false,
    body: ["M0,-30C9,-30 12,-22 12,-17C12,-11 7,-7 3,-5H-3C-7,-7 -12,-11 -12,-17C-12,-22 -9,-30 0,-30Z", rect(0, 20, 5.2, 4)],
    detail: [],
    lines: ["M-3,-5L-2.4,16M3,-5L2.4,16"],
  },
  sat: {
    rotates: false,
    body: [rect(0, 0, 6, 6), rect(-17, 0, 9, 4.4), rect(17, 0, 9, 4.4)],
    detail: ["M-14,-4.4V4.4M-20,-4.4V4.4M14,-4.4V4.4M20,-4.4V4.4"],
    lines: ["M0,-6V-14"],
  },
  aprs: { rotates: false, body: ["M0,-20L20,0L0,20L-20,0Z"], detail: ["M0,-8L8,0L0,8L-8,0Z"] },
  aprs_car: {
    rotates: false,
    body: ["M-9,-18C-9,-22 9,-22 9,-18L10,18C10,23 -10,23 -10,18Z"],
    detail: ["M-6.5,-12H6.5L7.5,-5H-7.5Z", "M-6.5,10H6.5V14H-6.5Z"],
  },
  aprs_house: {
    rotates: false,
    body: ["M0,-22L22,-2H15V20H-15V-2H-22Z"],
    detail: ["M-4,20V8H4V20"],
  },
  aprs_digi: {
    rotates: false,
    body: ["M0,-8L9,22H-9Z"],
    detail: ["M-5,14H5"],
    lines: ["M-8,-12A11,11 0 0,1 8,-12M-14,-18A19,19 0 0,1 14,-18"],
  },
  ham: { rotates: false, body: ["M-2,-24H2V16L12,26H-12L-2,16Z"], detail: [] },
  buoy: { rotates: false, body: ["M0,6m-13,0a13,13 0 1,0 26,0a13,13 0 1,0 -26,0", "M-2,-22H2V0H-2Z"], detail: ["M-13,6H13"] },
  dsc: { rotates: false, body: ["M-16,8L-10,-4H10L16,8L12,18H-12Z"], detail: ["M-5,-4V-12H5V-4"] },
  station: { rotates: false, body: ["M0,22L18,-8H-18Z"], detail: ["M0,10V-2"] },
};

/** Look a shape up with a safe fallback. */
export function shapeOf(name) {
  return SHAPES[name] || SHAPES.narrow;
}
