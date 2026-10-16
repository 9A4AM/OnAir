// types.test.js - tests for public/types.js: ICAO type / ADS-B category / AIS type -> icon shape.
import test from "node:test";
import assert from "node:assert/strict";
import { aircraftShape, vesselShape, aprsShape, iconShape, familyOfCategory, isMilitaryHex } from "../public/types.js";
import { SHAPES } from "../public/shapes.js";

test("airliners by ICAO type code", () => {
  assert.equal(aircraftShape({ icaoType: "A320" }), "narrow");
  assert.equal(aircraftShape({ icaoType: "B77W" }), "wide");
  assert.equal(aircraftShape({ icaoType: "A388" }), "quad");
  assert.equal(aircraftShape({ icaoType: "B744" }), "quad");
  assert.equal(aircraftShape({ icaoType: "E190" }), "regional");
  assert.equal(aircraftShape({ icaoType: "AT72" }), "turboprop");
  assert.equal(aircraftShape({ icaoType: "C172" }), "light");
  assert.equal(aircraftShape({ icaoType: "EC45" }), "heli");
  assert.equal(aircraftShape({ icaoType: "F16" }), "military");
});

test("falls back to emitter category, then military hex, then narrow", () => {
  assert.equal(aircraftShape({ cat: "A1" }), "light");
  assert.equal(aircraftShape({ cat: "A5" }), "wide");
  assert.equal(aircraftShape({ cat: "A7" }), "heli");
  assert.equal(aircraftShape({ cat: "B1" }), "glider");
  assert.equal(aircraftShape({ cat: "B2" }), "balloon");
  assert.equal(aircraftShape({ cat: "B6" }), "drone");
  assert.equal(aircraftShape({ hex: "AE1234" }), "military");
  assert.equal(aircraftShape({}), "narrow");
  assert.equal(aircraftShape({ icaoType: "A320", cat: "A5" }), "narrow"); // type code wins
  assert.equal(familyOfCategory("garbage"), "");
  assert.equal(isMilitaryHex("3C6444"), false);
});

test("ships by AIS type text or code", () => {
  assert.equal(vesselShape({ cat: "Cargo (70)" }), "ship_cargo");
  assert.equal(vesselShape({ cat: "Tanker" }), "ship_tanker");
  assert.equal(vesselShape({ extra: "type 60" }), "ship_passenger");
  assert.equal(vesselShape({ cat: "Fishing" }), "ship_fishing");
  assert.equal(vesselShape({ cat: "Sailing" }), "ship_sailing");
  assert.equal(vesselShape({ cat: "Tug" }), "ship_tug");
  assert.equal(vesselShape({}), "ship_other");
});

test("APRS and other layers", () => {
  assert.equal(aprsShape({ extra: "digipeater WIDE1" }), "aprs_digi");
  assert.equal(aprsShape({ extra: "home weather station" }), "aprs_house");
  assert.equal(aprsShape({ extra: "mobile" }), "aprs_car");
  assert.equal(aprsShape({}), "aprs");
  assert.equal(iconShape({ kind: "sonde" }), "sonde");
  assert.equal(iconShape({ kind: "sat" }), "sat");
  assert.equal(iconShape({ kind: "flarm", cat: "Glider" }), "glider");
});

test("every shape name the mapping can return has geometry", () => {
  const names = ["narrow", "wide", "quad", "regional", "turboprop", "light", "heli", "military", "glider", "balloon", "drone",
    "ship_cargo", "ship_tanker", "ship_passenger", "ship_fishing", "ship_sailing", "ship_tug", "ship_other",
    "sonde", "sat", "aprs", "aprs_car", "aprs_house", "aprs_digi", "ham", "buoy", "dsc", "station"];
  for (const n of names) assert.ok(SHAPES[n] && SHAPES[n].body.length, n);
});
