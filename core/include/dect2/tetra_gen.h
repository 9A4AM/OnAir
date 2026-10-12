// TETRA test signal: base station carriers sending a continuous downlink as a real cell does: the synchronisation burst (BSCH) and
// the SYSINFO broadcast (BNCH) in frame 18, the main control channel on timeslot 1 with the neighbour cell broadcast (D-NWRK-BROADCAST)
// and, on the cell that uses encryption, encrypted signalling (random bits), a traffic timeslot of random bits, and idle null PDUs
// elsewhere; every slot with its access assignment (AACH). It uses the test network code MCC 1, MNC 1, not a real network.
// Nothing is transmitted: the samples only exist in memory.
//
// SynthConfig (makeTetraSynth):
//   snrDb       signal to noise inside the 25 kHz channel of the strongest carrier
//   cfoHz       the radio's tuning error
//   sroPpm      the base station's symbol clock relative to the radio's sample clock
//   modeOpt[0]  scene: 0 one cell (392.4125 MHz, encrypted network), 1 two cells (392.4125 and 392.6000 MHz, the second in the clear)
// The carriers sit at their frequencies relative to a radio centred on 392.5 MHz (the mode's default).
#pragma once
#include "mode_synth.h"
#include "tetra_pdu.h"
#include <memory>
#include <vector>

namespace dect2 {

struct SynthConfig;

struct TetraCellCfg {
    tetra::Sync sync;              // MCC, MNC, colour code, sharing mode (tn / fn / mn are set per burst)
    tetra::Sysinfo si;             // main carrier, band, location area, services
    tetra::NwrkBroadcast nwrk;     // neighbours, network time
    bool encryptedSignalling = false;   // MCCH PDUs sent with encryption mode 1 (random content)
    int trafficTs = 2;             // a timeslot marked as traffic (random bits), 0 none
    float amp = 0.25f;
    uint32_t startSlot = 0;        // the slot counter (multiframe, frame, timeslot) at the start of the signal
};

constexpr double kTetraTestCentreHz = 392.5e6;

int tetraTestCells(int scene);
TetraCellCfg tetraTestCell(int scene, int index);

namespace tetra {
// The 510 bits of one downlink slot of this cell; abs counts slots from multiframe 1, frame 1, timeslot 1
Bits cellSlot(const TetraCellCfg& c, uint32_t abs);
}

std::unique_ptr<ModeSynth> makeTetraSynth(const SynthConfig& cfg, double sampleRate);

} // namespace dect2
