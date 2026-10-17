// The table of modes the signal identifier knows (sig_ident.h): one row per mode the app can decode, what its signal looks like.
// Adding a mode: one row in sig_known.cpp (docs/signal-id.md), then run test_specx_idmatrix and look at its line in the table.
#pragma once
#include "dect2/engine/sig_ident.h"

namespace dect2 {
namespace sigid {

enum SyncKind { SyncNone = 0, SyncDmr, SyncP25, SyncNxdn, SyncDpmr, SyncPocsag, SyncAis, SyncCount };

constexpr unsigned MB(int m) { return 1u << m; }   // a modulation class in a mask

struct Known {
    const char* name; const char* modeId; const char* subId; const char* family;   // family: the group the accuracy table is kept by
    unsigned mods;                 // the classes the signal may show as (MB(ModFsk4) | ...)
    int mod;                       // the one it should show as (it scores more)
    double rates[20];               // symbol rates; OFDM: useful symbol lengths in seconds; 0 ends the list
    double bw[6];                  // occupied widths (99 %), Hz, the usual ones; 0 ends the list; for AM / FM / carriers an upper limit
    double devOuter;               // FSK outer deviation, Hz (0 not FSK)
    int duty;                      // 0 either, 1 continuous, 2 bursty
    double burstMs;                // length of a burst when the protocol fixes it (0 free)
    double periodMs;               // spacing of the bursts when the protocol fixes it (0 free)
    int sync;
    float bands[10];               // MHz (lo, hi) pairs, 0 = end; none = anywhere
    const char* note;
};
extern const Known kKnown[];
extern const int kKnownCount;

} // namespace sigid
} // namespace dect2
