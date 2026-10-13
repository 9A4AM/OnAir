// Engine: owns the source, the sample ring and the analysis thread. UI talks only to this.
#pragma once
#include "dect2/iq_correct.h"
#include "dect2/offset_tune.h"
#include "dect2/exact_resampler.h"
#include "dect2/delivery_check.h"
#include "source.h"
#include "iq_record.h"
#include "spectrum.h"
#include "t2rx.h"
#include "dvbt_rx.h"
#include "atsc_rx.h"
#include "dab.h"
#include "atsc3_rx.h"
#include "isdbt_rx.h"
#include "fm_rx.h"
#include "dvbs_rx.h"
#include "dtmb_rx.h"
#include "atv_rx.h"
#include "dmr_rx.h"
#include "wide_modes.h"
#include "drm_rx.h"
#include "adsb_rx.h"
#include "gnss_rx.h"
#include "sonde_rx.h"
#include "ais_rx.h"
#include "marine_rx.h"
#include "acars_rx.h"
#include "inmc_rx.h"
#include "aero_rx.h"
#include "iridium_rx.h"
#include "mesh_rx.h"
#include "hdr_rx.h"
#include "cdr_rx.h"
#include "pager_rx.h"
#include "packet_rx.h"
#include "hfdig_rx.h"
#include "mode_tuning.h"
#include "airband_rx.h"
#include "vdl2_rx.h"
#include "ism_rx.h"
#include "vor_rx.h"
#include "m17_rx.h"
#include "hfdl_rx.h"
#include "cell_rx.h"
#include "cw_rx.h"
#include "dstar_rx.h"
#include "nxdn_rx.h"
#include "dpmr_rx.h"
#include "tetra_rx.h"
#include "mac_rx.h"
#include "gsmr_rx.h"
#include "dvbc_rx.h"
#include "orbcomm_rx.h"
#include "uplink_rx.h"
#include "ale_rx.h"
#include "p25_rx.h"
#include "ble_rx.h"
#include "iono_rx.h"
#include "psk_rx.h"
#include "js8_rx.h"
#include "specx_rx.h"
#include "sig_ident.h"
#include "specan_rx.h"
#include "astro_rx.h"
#include "qo100_rx.h"
#include "wifi_rx.h"
#include "general_rx.h"
#include "time_rx.h"
#include "cubesat_rx.h"
#include "globalstar_rx.h"
#include "vdes_rx.h"
#include "zigbee_rx.h"
#include "flarm_rx.h"
#include "hfbeacon_rx.h"
#include "dji_rx.h"
#include "nbiot_rx.h"
#include "halow_rx.h"
#include "codar_rx.h"
#include "wb_rx.h"
#include "datawell_rx.h"
#include "same_rx.h"
#include "ant_rx.h"
#include "multitone_rx.h"
#include "loraraw_rx.h"
#include "rid_rx.h"
#include "meteor_rx.h"
#include "nr_rx.h"
#include "goes_rx.h"
#include "hrpt_rx.h"
#include "bbunpack.h"
#include "multitv.h"
#include "ts.h"
#include "tsout.h"
#include "player.h"
#include "teletext.h"
#include "bandwidth.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdint>
#include <map>
#include <utility>

namespace dect2 {

// Wait/hold times of the analysis thread around the transport-stream lock (diagnostics, shown by the GUI status log).
std::string engineWaitProfile();

struct FileOptions {
    std::string path;
    FileFormat format = FileFormat::CS8;
    double sampleRate = 18285714.2857;
    bool loop = true;
    uint64_t offsetBytes = 0, endBytes = 0;   // SigMF / WAV: where the samples start and end in the file
    bool bigEndian = false;
};

// Where samples were lost since the radio was started. The radio side: samples that never came out of the radio (its USB link or driver
// could not keep up, or the computer slept), found by comparing what arrived with the radio's sample rate. The OnAir side: samples the
// receiver had to skip because it could not keep up with the rate (a slow or busy processor). The two need opposite remedies.
struct SampleLoss {
    bool live = false;                 // a live radio (files and the test signal never lose samples: they wait for the receiver)
    double radioSec = 0, cpuSec = 0;   // seconds of signal lost on each side
    uint64_t radioEvents = 0, cpuEvents = 0;
    double sinceRadioSec = -1, sinceCpuSec = -1;   // seconds since the last loss of each kind, -1 = none yet
    float loadPct = 0;                 // the receiver's share of real time on the sample thread over the last seconds (100 = just keeping up)
};

class Engine {
    int receiverId_ = 1;
public:
    Engine();
    ~Engine();

    bool start(const DeviceInfo& dev, const TuneSettings& tune, const FileOptions& file);
    // The number of this receiver (RX 1, RX 2 ...): the radio it opens is claimed for it (device_claims.h), another receiver cannot open it too
    void setReceiverId(int id) { receiverId_ = id; }
    int receiverId() const { return receiverId_; }
    void stop();
    bool retune(const TuneSettings& tune);
    // Retune and flush every stage (receiver, spectrum, transport stream) - used by the channel scanner.
    bool retuneReset(const TuneSettings& tune);
    uint64_t resetCount() const { return resets_.load(); }
    bool running() const { return running_; }
    // What the radio's driver said without failing: at the last retune (a frequency outside what the radio covers, ...) or else at the start
    // (a lower sample rate than asked for, ...); empty when it had nothing to say. The app shows it with the radio messages.
    std::string sourceNote() const { std::lock_guard<std::mutex> lk(noteMu_); return retuneNote_.empty() ? note_ : retuneNote_; }
    // A live radio that has stopped delivering samples (unplugged, or its USB connection broke). A HackRF is reopened when it comes back.
    bool radioLost() const { return radioLost_; }
    void setComputeMode(int m) { rx_.setComputeMode(m); rxDvbs_.setComputeMode(m); }
    void selectPlp(int id) { rx_.selectPlp(id); }
    // Which standard to decode: 0 = automatic (alternates between DVB-T2 and DVB-T until one locks), 1 = DVB-T2, 2 = DVB-T, 3 = ATSC, 4 = DAB, 5 = ATSC 3.0, 6 = ISDB-T, 7 = FM,
    // 8 = DVB-S/S2, 9 = DTMB, 10 = analog TV, 11 = DMR, 12 = DRM, 13 = ADS-B, 14 = GNSS,
    // 15 = radiosonde, 16 = AIS, 17 = marine, 18 = ACARS, 19 = Inmarsat-C, 20 = Inmarsat Aero, 21 = Iridium, 22 = mesh,
    // 23 = HD Radio, 24 = CDR, 25 = pagers, 26 = APRS / packet, 27 = HF digital, 28 = airband, 29 = VDL Mode 2, 30 = ISM sensors, 32 = VOR / ILS, 33 = M17, 34 = cell towers, 35 = D-STAR, 36 = CW, 37 = HFDL, 38 = PSK31, 39 = JS8Call, 42 = DVB-C, 46 = Orbcomm, 40 = NXDN, 41 = GSM-R, 45 = Mode S uplink, 47 = dPMR, 51 = HF ALE, 48 = TETRA, 49 = MAC, 50 = radio astronomy, 51 = HF ALE, 52 = P25, 53 = Bluetooth LE, 54 = ionospheric sounders
    void setStandard(int m) { stdMode_ = m; stdReq_ = true; refreshIdle(); }
    int standardMode() const { return stdMode_.load(); }
    int activeStandard() const { return activeStd_.load(); }  // 0 = DVB-T2, 1 = DVB-T, 2 = ATSC, 3 = DAB, 4 = ATSC 3.0, 5 = ISDB-T, 6 = FM, 7 = DVB-S/S2, 8 = DTMB, 9 = analog TV, 10 = DMR, 11 = DRM, 12 = ADS-B, 13 = GNSS, 14 to 27 = radiosonde, AIS, marine, ACARS, Inmarsat-C, Inmarsat Aero, Iridium, mesh, HD Radio, CDR, pagers, APRS / packet, HF digital, airband, VDL Mode 2, ISM sensors, VOR / ILS, M17, 34 = D-STAR, 35 = CW, 36 = HFDL, 37 = PSK31, 38 = JS8Call, 41 = DVB-C, 44 = Mode S uplink, 45 = Orbcomm, 46 = dPMR, 39 = NXDN, 47 = TETRA, 48 = MAC, 49 = radio astronomy, 50 = HF ALE, 51 = P25, 52 = Bluetooth LE, 53 = ionospheric sounders , 54 = QO-100, 55 = Wi-Fi networks, 56 = general receiver (always the standard code minus one)
    double sampleRate() const { return rate_; }
    double radioRate() const { return srcRate_; }   // the rate the radio (or file) runs at, as it confirmed it at the start
    // Channel bandwidth: with automatic detection on, the engine measures the width of the signal in the spectrum while nothing is
    // locked and reconfigures the receivers by itself. setBandwidth() forces a value (used by the scanner).
    void setBandwidthAuto(bool on) { if (bwAuto_.exchange(on) != on) bwReset_ = true; }
    void setBandwidth(double mhz) { bwReqVal_ = mhz; bwReq_ = true; }
    double activeBandwidth() const { return bwActive_.load(); }
    double detectedBandwidth() const { return bwDetected_.load(); }   // last bandwidth the detector settled on (0 = none yet)
    // Record the radio's raw samples (as they leave the ring, before any clean-up) at the radio's own rate. Not for a file source.
    bool startRecording(const std::string& path, FileFormat fmt, std::string& err);
    void stopRecording() { rec_.stop(); }
    void setRecordSigmf(bool on) { recSigmf_ = on; }   // SigMF metadata beside the recordings (default on)
    RecordingStats recordingStats() const { return rec_.stats(); }
    // A file name for a recording of what the radio is tuned to right now (see recordingFileName())
    std::string recordingName(FileFormat fmt) const;
    uint64_t droppedSamples() const { return ring_.dropped(); }   // the OnAir side only, in samples (see sampleLoss())
    SampleLoss sampleLoss() const;
    std::string loadProfile() const;   // seconds the analysis thread spent on the spectrum and on the receiver, and the signal it has processed (diagnostics)

    // Latest spectrum snapshot; returns true if newer than `lastSeq`.
    bool latestSpectrum(SpectrumFrame& out, uint64_t lastSeq);
    // no spectrum fft when headless, autobw needs it
    void setSpectrumEnabled(bool on) { analyzer_.setTransform(on); }

    bool latestRx(RxTelemetry& out, uint64_t lastSeq);

    // Two modes at once: a second receiver gets a copy of the same samples on its own thread (it never holds up the first: with a live radio
    // a second receiver that falls behind loses blocks, counted in secondaryDropped()). It is the idle receiver of its mode, shifted so that
    // its frequency sits where the mode expects it, so its own controls (acars(), vdl2(), ...) work as for the first mode. Only the modes of
    // secondaryAllowed(), only a pair that fits in the capture (secondaryCheck()); a refused or suspended pair says why in secondaryNote().
    // offsetHz: the second mode's frequency less captureCentreHz(); stdMode 0 = off. Kept over restarts and retunes (as a frequency).
    bool setSecondary(int stdMode, double offsetHz);
    bool setSecondaryHz(int stdMode, double freqHz);   // the same with the second mode's own frequency
    void clearSecondary() { setSecondary(0, 0); }
    int secondaryStandard() const { return secStd_.load(); }   // the engine standard code asked for, 0 = none
    bool secondaryRunning() const { return secAct_.load() >= 0; }
    double secondaryHz() const { return secHz_.load(); }
    std::string secondaryNote() const { std::lock_guard<std::mutex> lk(secNoteMu_); return secNote_; }
    uint64_t secondaryDropped() const { return secDropped_.load(); }   // samples the second receiver lost (a live radio only)
    double secondaryRate() const { return secRate_.load(); }   // the rate it decodes at: its mode's own, the capture resampled down to it
    // Its sound: off by default (the first mode keeps the speakers); applies to the modes that play any (DMR, dPMR, airband, CW, ...)
    void setSecondarySound(bool on);
    bool latestRx2(RxTelemetry& out, uint64_t lastSeq);   // the second mode's report (as latestRx)
    double captureCentreHz() const;   // the frequency at the centre of the samples the receivers get
    // The modes that can run second: narrow channels that neither retune the radio, nor make a transport stream, picture or player sound
    static bool secondaryAllowed(int stdMode);
    // Empty when the second mode fits beside the first in a capture of rateHz around centreHz, else the reason (shown to the user)
    static std::string secondaryCheck(int primaryStd, int secStd, double centreHz, double rateHz, double secHz);

    void setPlpDump(std::function<void(const PlpResult&)> cb) { plpDump_ = std::move(cb); }

    // Multi-channel TV (DVB-T / DVB-T2, multitv.h): when the capture is wide enough for more than one channel of the chosen width, one receiver runs for
    // every occupied channel of the raster (up to maxChannels), each with its own telemetry and service list; one channel plays at a time while the others
    // keep decoding. grid: 0 finds out whether the capture is centred on a channel or between two, 1 on a channel, 2 between two. Off: the single receiver as before.
    void setMultiChannel(bool on, int maxChannels = 0, int grid = 0);
    bool multiChannel() const { return mtvOn_.load(); }                  // asked for
    bool multiChannelActive() const { return mtvActive_.load(); }        // running now (the mode, the span and the width allow it)
    static bool multiChannelSupported(int stdMode) { return stdMode == 1 || stdMode == 2; }   // DVB-T2, DVB-T
    static int defaultMaxChannels();                                      // 2 on a laptop, 3 on a desktop
    static int slotsInSpan(double spanRate, double widthMhz, int grid);   // channels of the raster that fit the capture
    void selectTvChannel(int slot);                                       // the channel that plays and shows in detail
    int selectedTvChannel() const { auto m = mtvGet(); return m ? m->selected() : 0; }
    std::vector<TvChannelRow> tvChannels();
    std::vector<MultiService> multiServices();                            // every service of every channel, with the channel it comes from
    std::string multiChannelNote() const { std::lock_guard<std::mutex> lk(mtvNoteMu_); return mtvNote_; }
    // Transport stream (phase 4)
    TsSnapshot tsSnapshot();
    BbStats bbStats();
    std::map<int, std::vector<EpgEvent>> epg();
    // Every burst of transport stream packets is also handed to this function (the network tuner); it runs on the receiver thread, so it must be quick.
    void setPacketTap(std::function<void(const uint8_t*, size_t, const TsSnapshot*)> f);
    void setOutputs(const OutputConfig& c) { outputs_.configure(c); }
    OutputConfig outputConfig() const { return outputs_.config(); }
    OutputStats outputStats() const { return outputs_.stats(); }
    Player& player() { return player_; }
    const Player& player() const { return player_; }
    // DC spike removal and IQ imbalance correction of the radio's samples (iq_correct.h), off by default
    IqCorrector& iqFix() { return iqFix_; }
    const IqCorrector& iqFix() const { return iqFix_; }
    // Offset tuning, the alternative to removing the DC spike: the radio is tuned beside the channel so that its spike falls outside it.
    // Radios only, for the modes with one channel on the centre; a radio too slow for it gets the DC removal instead. Applies at start().
    void setAutoOffset(bool on) { autoOffset_ = on; }
    bool autoOffset() const { return autoOffset_; }
    double offsetHz() const { return offHz_; }   // the offset in use, 0 = none
    // DAB / DAB+: the ensemble found on the channel, and the station to play (a sub-channel id, -1 = none)
    DabEnsemble dabEnsemble() const { return rxD_.ensemble(); }
    DabFicAnalysis dabFic() const { return rxD_.ficAnalysis(); }
    void dabSelect(int subId) { rxD_.select(subId); }
    int dabSelected() const { return rxD_.selected(); }
    void dabSelectJournaline(int sub, int packetAddress, int ext) { rxD_.selectJournaline(sub, packetAddress, ext); }
    dect2::JournalineDecoder& dabJournaline() { return rxD_.journaline(); }
    DabAudio& dabAudio() { return rxD_.audio(); }
    FmReceiver& fm() { return rxFm_; }   // volume, mute and de-emphasis of the FM receiver
    DvbsReceiver& dvbs() { return rxDvbs_; }   // DVB-S/S2: the receiver's own controls and results (the interface reads telemetry through latestRx)
    DtmbReceiver& dtmb() { return rxDtmb_; }   // DTMB: the receiver's own controls and results (the interface reads telemetry through latestRx)
    AtvReceiver& atv() { return rxAtv_; }   // Analog TV: the receiver's own controls and results (the interface reads telemetry through latestRx)
    DmrReceiver& dmr() { return rxDmr_; }   // DMR: the receiver's own controls and results (the interface reads telemetry through latestRx)
    DrmReceiver& drm() { return rxDrm_; }   // DRM: the receiver's own controls and results (the interface reads telemetry through latestRx)
    AdsbReceiver& adsb() { return rxAdsb_; }   // ADS-B: the receiver's own controls and results (the interface reads telemetry through latestRx)
    GnssReceiver& gnss() { return rxGnss_; }   // GNSS: the receiver's own controls and results (the interface reads telemetry through latestRx)
    SondeReceiver& sonde() { return rxSonde_; }   // Radiosonde: the receiver's own controls and results (the interface reads telemetry through latestRx)
    AisReceiver& ais() { return rxAis_; }   // AIS: the receiver's own controls and results (the interface reads telemetry through latestRx)
    MarineReceiver& marine() { return rxMarine_; }   // Marine: the receiver's own controls and results (the interface reads telemetry through latestRx)
    AcarsReceiver& acars() { return rxAcars_; }   // ACARS: the receiver's own controls and results (the interface reads telemetry through latestRx)
    InmcReceiver& inmc() { return rxInmc_; }   // Inmarsat-C: the receiver's own controls and results (the interface reads telemetry through latestRx)
    AeroReceiver& aero() { return rxAero_; }   // Inmarsat Aero: the receiver's own controls and results (the interface reads telemetry through latestRx)
    IridiumReceiver& iridium() { return rxIridium_; }   // Iridium: the receiver's own controls and results (the interface reads telemetry through latestRx)
    MeshReceiver& mesh() { return rxMesh_; }   // Mesh (LoRa): the receiver's own controls and results (the interface reads telemetry through latestRx)
    HdrReceiver& hdr() { return rxHdr_; }   // HD Radio: the receiver's own controls and results (the interface reads telemetry through latestRx)
    CdrReceiver& cdr() { return rxCdr_; }   // CDR: the receiver's own controls and results (the interface reads telemetry through latestRx)
    PagerReceiver& pager() { return rxPager_; }   // Pagers: the receiver's own controls and results (the interface reads telemetry through latestRx)
    PacketReceiver& packet() { return rxPacket_; }   // APRS / packet: the receiver's own controls and results (the interface reads telemetry through latestRx)
    HfdigReceiver& hfdig() { return rxHfdig_; }   // HF digital: the receiver's own controls and results (the interface reads telemetry through latestRx)
    Vdl2Receiver& vdl2() { return rxVdl2_; }   // VDL Mode 2: the receiver's own controls and results (the interface reads telemetry through latestRx)
    GsmrReceiver& gsmr() { return rxGsmr_; }   // GSM-R: the receiver's own controls and results (the interface reads telemetry through latestRx)
    CellReceiver& cell() { return rxCell_; }   // Cell towers: the receiver's own controls and results (the interface reads telemetry through latestRx)
    IsmReceiver& ism() { return rxIsm_; }   // ISM sensors: the receiver's own controls and results (the interface reads telemetry through latestRx)
    // Whole-span decoding (the "wide decode" switch): the narrowband HF modes (CW, PSK, HF digital: FT8 / FT4 / WSPR slots and RTTY) decode
    // the visible span (up to 250 kHz around the dial frequency) instead of the tuned transmission (widespan.h). Off by default.
    void setWideDecode(bool on, double spanHz = 250000);
    bool wideDecode() const { return wideDecode_.load(); }
    CwReceiver& cw() { return rxCw_; }   // CW: the receiver's own controls and results (the interface reads telemetry through latestRx)
    MacReceiver& mac() { return rxMac_; }   // MAC television: pictures, sound (the interface reads telemetry through latestRx)
    DpmrReceiver& dpmr() { return rxDpmr_; }   // dPMR: scan, sound (the interface reads telemetry through latestRx)
    NxdnReceiver& nxdn() { return rxNxdn_; }   // NXDN: the receiver's own controls and results (the interface reads telemetry through latestRx)
    SpecxReceiver& specx() { return rxSpecx_; }   // spectrum explorer: FFT size and averaging
    sigid::Capture& sigid() { return sigid_; }   // signal identifier: start() a capture of a region of what the receivers see, in any mode
    AstroReceiver& astro() { return rxAstro_; }   // radio astronomy (hydrogen line): FFT size, integration, frequency switching
    SpecanReceiver& specan() { return rxSpecan_; }   // spectrum analyser: range, resolution, averaging (telemetry through latestRx)
    Js8Receiver& js8() { return rxJs8_; }   // JS8Call: the receiver's own controls and results (the interface reads telemetry through latestRx)
    OrbcommReceiver& orbcomm() { return rxOrb_; }   // Orbcomm: downlinks at once (the interface reads telemetry through latestRx)
    TetraReceiver& tetra() { return rxTetra_; }   // TETRA network information: the interface reads telemetry through latestRx
    AleReceiver& ale() { return rxAle_; }   // HF ALE: the receiver's own controls and results (the interface reads telemetry through latestRx)
    P25Receiver& p25() { return rxP25_; }   // P25 Phase 1: sound (the interface reads telemetry through latestRx)
    UplinkReceiver& uplink() { return rxUplink_; }   // Mode S uplink (1030 MHz): telemetry through latestRx
    BleReceiver& ble() { return rxBle_; }   // Bluetooth LE advertising: clear the device table (telemetry through latestRx)
    IonoReceiver& iono() { return rxIono_; }   // ionospheric sounders: timing lock, the sounder shown (telemetry through latestRx)
    GeneralReceiver& general() { return rxGeneral_; }   // general receiver: demodulator, filter, squelch, sound and recording (telemetry through latestRx)
    GlobalstarReceiver& globalstar() { return rxGs_; }   // Globalstar activity monitor: clear the event log (telemetry through latestRx)
    VdesReceiver& vdes() { return rxVdes_; }   // VDES: clear the ASM and burst lists (telemetry through latestRx)
    NbiotReceiver& nbiot() { return rxNb_; }   // NB-IoT cells: clear the cell table (telemetry through latestRx)
    ZigbeeReceiver& zigbee() { return rxZb_; }   // Zigbee / 802.15.4: clear the PAN and device tables (telemetry through latestRx)
    FlarmReceiver& flarm() { return rxFlarm_; }   // FLARM: receiver position, clear (telemetry through latestRx)
    LoraRawReceiver& loraraw() { return rxLr_; }   // LoRa (raw): settings, clear the frame log (telemetry through latestRx)
    HfbReceiver& hfbeacon() { return rxHfb_; }   // HF beacons: the list, receiver position, ppm, clock offset (telemetry through latestRx)
    DjiReceiver& dji() { return rxDji_; }   // DJI drones: clear the results (telemetry through latestRx)
    WbReceiver& wb() { return rxWb_; }   // Signal workbench: nothing to clear (the tool works on files)
    DatawellReceiver& datawell() { return rxDw_; }   // Datawell buoy HF link: clear the frames, force the format (telemetry through latestRx)
    MultitoneReceiver& multitone() { return rxMt_; }   // Multitone paging: clear the transmission list (telemetry through latestRx)
    CodarReceiver& codar() { return rxCd_; }   // CODAR / HF ocean radar: clear the station table and the events (telemetry through latestRx)
    AntReceiver& ant() { return rxAnt_; }   // ANT+ sensors: the sensor table and frame log (telemetry through latestRx)
    SameReceiver& same() { return rxSame_; }   // NOAA weather radio SAME: the alert list, country, Listen (telemetry through latestRx)
    HalowReceiver& halow() { return rxHl_; }   // Wi-Fi HaLow / 802.11ah: clear the network table and the frame log (telemetry through latestRx)
    CubesatReceiver& cubesat() { return rxCube_; }   // CubeSat telemetry: satellite table, tracked satellite, KISS (telemetry through latestRx)
    TimeReceiver& timesig() { return rxTime_; }   // time signals: station, receiver position (telemetry through latestRx)
    RidReceiver& rid() { return rxRid_; }   // Drone Remote ID: clear the drone table (telemetry through latestRx)
    HrptReceiver& hrpt() { return rxHrpt_; }   // HRPT / AHRPT: variant, new picture (telemetry through latestRx)
    GoesReceiver& goes() { return rxGoes_; }   // GOES HRIT: clear the products (telemetry through latestRx)
    MeteorReceiver& meteor() { return rxMeteor_; }   // Meteor-M LRPT: modulation, a new picture (telemetry through latestRx)
    NrReceiver& nr() { return rxNr_; }         // 5G NR cells: clear the cell table (telemetry through latestRx)
    WifiReceiver& wifi() { return rxWifi_; }   // Wi-Fi networks: clear the network table (telemetry through latestRx)
    Qo100Receiver& qo100() { return rxQo100_; }   // QO-100: LNB oscillator, SSB dial and listening (telemetry through latestRx)
    DvbcReceiver& dvbc() { return rxDvbc_; }   // DVB-C: symbol rate, QAM, annex (the interface reads telemetry through latestRx)
    PskReceiver& psk() { return rxPsk_; }   // PSK31: the receiver's own controls and results (the interface reads telemetry through latestRx)
    DstarReceiver& dstar() { return rxDstar_; }   // D-STAR: the receiver's own controls and results (the interface reads telemetry through latestRx)
    HfdlReceiver& hfdl() { return rxHfdl_; }   // HFDL: the receiver's own controls and results (the interface reads telemetry through latestRx)
    M17Receiver& m17() { return rxM17_; }   // M17: the receiver's own controls and results (the interface reads telemetry through latestRx)
    AirbandReceiver& airband() { return rxAirband_; }   // Airband: the receiver's own controls and results (the interface reads telemetry through latestRx)
    VorReceiver& vor() { return rxVor_; }   // VOR / ILS: the receiver's own controls and results (the interface reads telemetry through latestRx)
    TeletextDecoder& teletext() { return ttx_; }
    // ATSC 3.0: the service list and statistics, and which service to receive (-1 = the first video service)
    bool atsc3Telemetry(Atsc3Telemetry& t) const { std::lock_guard<std::mutex> lk(atsc3Mu_); t = atsc3Tel_; return atsc3Tel_.seq != 0; }
    void atsc3Select(int serviceId) { rxA3_.selectService(serviceId); }

    // Decoded results: the lists every mode keeps (messages, aircraft, vessels, cells, pictures...) live through mode switches, retunes
    // and restarts of the receiver, and are saved to a folder (one file per receiver) so the next run starts with them. Only a mode's
    // Clear empties them. Any thread.
    // Whole-span decoding (DMR, M17, D-STAR, NXDN, dPMR, P25): one decoder per carrier found in the captured span instead of the tuned channel only.
    // Takes effect at once; the single-channel receiver is untouched while it is on. Needs a restart-free toggle: the carriers appear within a second.
    void setWideVoice(bool on) { wideOn_.store(on); for (auto& v : wideSeq_) v = 0; }
    bool wideVoice() const { return wideOn_.load(); }
    void setWideMuted(bool m) { wide_.setMuted(m); }
    void setWideSelected(int carrierId) { wide_.setSelected(carrierId); }   // the carrier whose sound plays and whose detail shows (0: the best locked)
    static bool wideSupported(int analysisMode) { return WideModes::supports(analysisMode); }
    void clearResults(int stdMode);                      // the Clear of a mode's screen: every list of its receiver
    int loadResults(const std::string& dir);             // the lists of the last run (a file of another build is discarded); the receivers restored
    int saveResults(const std::string& dir);             // the receivers whose lists changed since the last save (or load); the files written
    void setResultsDir(const std::string& dir);          // load from dir now, then save to it every 30 s on a thread of its own (empty: stop)
    void flushResults();                                 // save now (at exit; also done by the destructor) and stop the 30 s saver
    std::vector<std::string> resultNames() const;        // the file names (without .bin) of the receivers that keep results
    // With no radio running: the active mode's report again, so its kept lists show (after a load, a Clear or a change of mode)
    void refreshIdle();

    // Log
    void log(const std::string& line);
    std::vector<std::string> logSnapshot(size_t& total);
    void clearLog();

private:
    struct ResultSlot {
        const char* name;                                // file name: results/<name>.bin
        int act;                                         // the receiver's active standard (activeStandard())
        std::function<std::string()> save;
        std::function<bool(const std::string&)> load;
        std::function<void()> clear;
    };
    std::vector<ResultSlot> resultSlots();
    std::mutex resIoMu_;                                 // one save or load at a time; resHash_
    std::mutex resMu_;                                   // the saver thread: resDir_, resStop_
    std::string resDir_;
    std::map<std::string, uint64_t> resHash_;            // what each file holds now (fnv1a of the blob)
    std::thread resTh_;
    std::condition_variable resCv_;
    bool resStop_ = false;
    void stopResultsSaver();
    void analysisLoop();
    // the spectrum is computed on its own thread, so that it never holds up the receiver (it takes a seventh of a slow core)
    void spectrumLoop();
    void feedSpectrum(const cf32* x, size_t n);
    std::thread specTh_;
    std::mutex specQMu_;
    std::condition_variable specQCv_;
    std::deque<std::vector<cf32>> specQ_;
    bool specStop_ = false;
    void catchUp();
    void logRxEvents(const RxTelemetry& t);
    std::function<void(const PlpResult&)> plpDump_;
    void onPlp(const PlpResult& r);
    // multi-channel TV (setMultiChannel)
    std::shared_ptr<MultiTv> mtv_;
    mutable std::mutex mtvMu_;               // mtv_ (the analysis thread replaces it, the interface reads it)
    std::shared_ptr<MultiTv> mtvGet() const { std::lock_guard<std::mutex> lk(mtvMu_); return mtv_; }
    std::atomic<bool> mtvOn_{false}, mtvActive_{false};
    std::atomic<int> mtvMax_{0}, mtvGrid_{0};
    double mtvKeyRate_ = -1, mtvKeyBw_ = 0;
    int mtvKeyStd_ = -1, mtvKeyMax_ = -1, mtvKeyGrid_ = -1;
    bool mtvKeyOn_ = false;
    mutable std::mutex mtvNoteMu_;
    std::string mtvNote_;
    void syncMultiTv();                       // the analysis thread: starts, changes or stops the multi-channel receivers when the settings or the capture changed
    void onMultiPackets(const uint8_t* pk, size_t n, double secs, const TsSnapshot& snap);
    std::mutex tsMu_;
    BbUnpacker unpack_;
    TsDemux demux_;
    TsSnapshot tsSnap_;
    OutputManager outputs_;
    std::function<void(const uint8_t*, size_t, const TsSnapshot*)> tap_;
    Player player_;
    IqCorrector iqFix_;
    // offset tuning (offset_tune.h): the radio is tuned offHz_ below the channel at offRate_, and offMix_ brings the channel back to the centre;
    // offHz_ is 0 when it is not in use. offsetDc_: offset tuning was asked for but is not possible, the DC spike is removed instead
    std::atomic<bool> autoOffset_{false}, offsetDc_{false};
    std::atomic<double> offHz_{0}, offRate_{0};
    OffsetMixer offMix_;
    // with offset tuning the radio runs faster than the mode needs; right after the shift the samples go back to the mode's own rate, so the
    // receivers, the spectrum and the waterfall see exactly what they see without the offset (only the radio's USB stream is larger)
    ExactResampler offRs_;
    bool offResample_ = false;
    std::vector<cf32> offBuf_;
    double srcRate_ = 0;   // the radio's rate (rate_ is what the receivers get: the same, or the mode's rate with offset tuning)
    bool offsetAllowed() const;
    // the radio's samples into the receivers: clean-up, offset shift and resampling, spectrum, receiver, accounting
    void ingest(cf32* x, size_t n);
    TeletextDecoder ttx_;
    int lastT2Frame_ = -1;
    std::atomic<bool> resetReq_{false};
    std::atomic<uint64_t> resets_{0};
    void applyReset();
    std::vector<uint8_t> frameBuf_;
    double tSpec_ = 0, tRx_ = 0;
    double nSamp_ = 0;
    uint64_t logP1Count_ = 0;
    int logS1_ = -1, logS2_ = -1, logGi_ = -2, logState_ = -1, logFrameSyms_ = 0;

    IqRing ring_;
    // The log is declared before every receiver: members are destroyed in reverse order, and receiver worker
    // threads (joined in the receivers' destructors) may still log until then.
    std::mutex logMu_;
    std::vector<std::string> log_;
    std::chrono::steady_clock::time_point t0_;
    IqRecorder rec_;
    bool recSigmf_ = true;
    std::unique_ptr<IqSource> src_;
    SpectrumAnalyzer analyzer_{8192};
    T2Receiver rx_;
    DvbtReceiver rxT_;
    AtscReceiver rxA_;
    DabReceiver rxD_;
    Atsc3Rx rxA3_;
    IsdbtReceiver rxI_;
    FmReceiver rxFm_;
    DvbsReceiver rxDvbs_;
    DtmbReceiver rxDtmb_;
    AtvReceiver rxAtv_;
    DmrReceiver rxDmr_;
    WideModes wide_;
    std::atomic<bool> wideOn_{false};
    uint64_t wideSeq_[64] = {};
    DrmReceiver rxDrm_;
    AdsbReceiver rxAdsb_;
    GnssReceiver rxGnss_;
    SondeReceiver rxSonde_;
    AisReceiver rxAis_;
    MarineReceiver rxMarine_;
    AcarsReceiver rxAcars_;
    InmcReceiver rxInmc_;
    AeroReceiver rxAero_;
    IridiumReceiver rxIridium_;
    MeshReceiver rxMesh_;
    HdrReceiver rxHdr_;
    CdrReceiver rxCdr_;
    PagerReceiver rxPager_;
    PacketReceiver rxPacket_;
    HfdigReceiver rxHfdig_;
    AirbandReceiver rxAirband_;
    Vdl2Receiver rxVdl2_;
    IsmReceiver rxIsm_;
    VorReceiver rxVor_;
    M17Receiver rxM17_;
    HfdlReceiver rxHfdl_;
    CellReceiver rxCell_;
    CwReceiver rxCw_;
    std::atomic<bool> wideDecode_{false};
    DstarReceiver rxDstar_;
    NxdnReceiver rxNxdn_;
    DpmrReceiver rxDpmr_;
    MacReceiver rxMac_;
    P25Receiver rxP25_;
    GsmrReceiver rxGsmr_;
    PskReceiver rxPsk_;
    Js8Receiver rxJs8_;
    SpecxReceiver rxSpecx_;
    sigid::Capture sigid_;
    SpecanReceiver rxSpecan_;
    AstroReceiver rxAstro_;
    bool specanSweep_ = false;   // the HackRF sweeps by itself (spectrum analyser)
    DvbcReceiver rxDvbc_;
    OrbcommReceiver rxOrb_;
    UplinkReceiver rxUplink_;
    TetraReceiver rxTetra_;
    AleReceiver rxAle_;
    BleReceiver rxBle_;
    IonoReceiver rxIono_;
    Qo100Receiver rxQo100_;
    WifiReceiver rxWifi_;
    GeneralReceiver rxGeneral_;
    TimeReceiver rxTime_;
    CubesatReceiver rxCube_;
    GlobalstarReceiver rxGs_;
    VdesReceiver rxVdes_;
    ZigbeeReceiver rxZb_;
    HfbReceiver rxHfb_;
    DjiReceiver rxDji_;
    NbiotReceiver rxNb_;
    HalowReceiver rxHl_;
    CodarReceiver rxCd_;
    WbReceiver rxWb_;
    DatawellReceiver rxDw_;
    SameReceiver rxSame_;
    AntReceiver rxAnt_;
    MultitoneReceiver rxMt_;
    LoraRawReceiver rxLr_;
    RidReceiver rxRid_;
    MeteorReceiver rxMeteor_;
    NrReceiver rxNr_;
    GoesReceiver rxGoes_;
    HrptReceiver rxHrpt_;
    FlarmReceiver rxFlarm_;
    uint64_t modeSeq_[80] = {};      // the last report taken from each of the modes added after FM (same order as above)
    mutable std::mutex atsc3Mu_;
    Atsc3Telemetry atsc3Tel_;
    uint64_t atsc3Seq_ = 0;
    int logA3State_ = -1;
    bool logA3Svc_ = false;
    uint64_t dabSeq_ = 0;
    int dmbSub_ = -1;                // the DMB video sub-channel the player is showing (-1 = none)
    uint64_t fmSeq_ = 0;
    int logDState_ = -1;
    bool logDEns_ = false;
    void logDabEvents(const RxTelemetry& t);
    uint64_t atscSeq_ = 0;
    int logAState_ = -1;
    void logAtscEvents(const RxTelemetry& t);
    void logAtsc3Events(const Atsc3Telemetry& a);
    std::atomic<int> stdMode_{0}, activeStd_{0};
    FileOptions lastFile_;   // what start() got, for a restart when a retune changes the radio's sample rate
    bool restartIfRateChanged(const TuneSettings& tune);
    std::atomic<bool> stdReq_{false};
    double autoMark_ = 0, lastLockSec_ = 0;
    double bwMhz_ = 8;
    std::atomic<double> bwActive_{8}, bwDetected_{0}, bwReqVal_{0};
    std::atomic<bool> bwAuto_{false}, bwReq_{false}, bwReset_{false};
    BandwidthDetector bwDet_;
    double bwMark_ = 0, bwLastEval_ = 0;
    double bwVote_ = 0;
    void bandwidthStep(const SpectrumFrame& f, const RxTelemetry& t, bool tLocked);
    void changeBandwidth(double mhz);
    void feedRx(const cf32* x, size_t n);
    void feedReceiver(int a, const cf32* x, size_t n);   // the receiver of active standard a
    void resetReceiver(int a);
    // the report of the receiver of active standard a for the modes from DVB-S on: 1 = a new one in t, 0 = none new, -1 = not one of them
    int pollMode(int a, RxTelemetry& t, uint64_t* seqs);
    // the second mode (setSecondary): the queue to its thread, the receiver's placement in the capture
    void secondaryLoop(int a);
    void startSecondary();                    // start(): the second mode asked for, suspended when it does not fit
    void stopSecondary();                     // stop(): stays asked for
    bool startSecondaryLocked(bool keep);     // under secCtlMu_; keep: a pair that does not fit stays asked for (suspended)
    void stopSecondaryLocked();
    void refreshSecondary(bool force);        // after a retune: placed again (force: the radio moved), suspended or resumed
    bool placeSecondary(std::string& why);   // checks the pair with the current tuning and shifts the receiver there; false = refused
    void placeReceiver(int a, double sigOffHz, double centreHz, double dialHz, double chanHz);
    void placeReceiverAsPrimary(int a);      // back to what start() gave it
    void configureReceiver(int a, double rateHz);
    void muteReceiver(int a, bool muted);
    std::atomic<bool> secSound_{false};
    std::atomic<double> secRate_{0};          // the second receiver's rate: its mode's own, the capture resampled down to it
    void pushSecondary(const cf32* x, size_t n);
    void setSecNote(const std::string& s) { std::lock_guard<std::mutex> lk(secNoteMu_); secNote_ = s; }
    std::thread secTh_;
    std::mutex secQMu_;
    std::condition_variable secQCv_;
    std::deque<std::vector<cf32>> secQ_;
    size_t secQN_ = 0;                  // samples in secQ_
    bool secStop_ = false;
    std::mutex secFeedMu_;              // held while the second receiver takes a block: nothing resets or moves it meanwhile
    std::mutex secCtlMu_;               // setSecondary / start / stop of the second mode
    std::atomic<int> secStd_{0};        // asked for: engine standard code, 0 = none
    std::atomic<double> secHz_{0};      // its frequency
    std::atomic<int> secAct_{-1};       // the receiver (active standard) running as the second mode, -1 = none
    std::atomic<double> secShift_{0};   // how far its frequency lies above the centre of the capture
    std::atomic<bool> secFlush_{false};
    std::atomic<uint64_t> secDropped_{0};
    uint64_t modeSeq2_[80] = {};
    std::mutex rx2Mu_;
    RxTelemetry rxTel2_;
    uint64_t rx2Pub_ = 0;
    mutable std::mutex secNoteMu_;
    std::string secNote_;
    void autoSelect(const RxTelemetry& t, bool tLocked);
    void onTsPackets(const uint8_t* pk, size_t n, double secs);
    void logDvbtEvents(const RxTelemetry& t);
    void logIsdbtEvents(const RxTelemetry& t);
    int logIMode_ = -1, logIGi_ = -1, logITmcc_ = -1; bool logISync_ = false;
    int logTMode_ = -1, logTGi_ = -1, logTTps_ = -1; bool logTFec_ = false;
    std::mutex rxMu_;
    RxTelemetry rxTel_;
    // Every receiver counts its reports from 1, so after a change of mode (or a restart) its numbers would lie below the last one the interface
    // took and latestRx() would hold them back. The published report therefore carries a number of the engine that only grows. Under rxMu_.
    uint64_t rxPub_ = 0;
    void publishRx(RxTelemetry&& t) { t.seq = ++rxPub_; rxTel_ = std::move(t); }
    std::thread th_;
    std::atomic<bool> running_{false}, stopReq_{false};
    void watchRadio();
    std::atomic<bool> radioLost_{false};
    TuneSettings hwTune(const TuneSettings& t) const;      // radioTune() through the converter (converter.h): what the radio itself is tuned to
    mutable std::atomic<bool> convSwap_{false};            // the converter mirrors the spectrum: ingest() swaps I and Q
    TuneSettings radioTune(const TuneSettings& t) const;  // what the radio is tuned to: the user's frequency plus the mode's tuneOffsetHz
    mutable std::mutex tuneMu_;                 // lastDev_ / lastTune_: what to reopen the radio with
    DeviceInfo lastDev_;
    TuneSettings lastTune_;
    std::chrono::steady_clock::time_point lastSamples_{}, nextReconnect_{}, lastSkipLog_{};
    uint64_t skippedSamples_ = 0, skipEvents_ = 0;   // analysis thread only
    // sampleLoss(): the radio's deliveries checked against its rate (checkDelivery(), analysis thread), the OnAir side from the ring
    void checkDelivery();
    void holdDelivery(int ms);   // start the check again after a start, retune or reset (the radio may pause around them)
    std::atomic<int64_t> delivHoldNs_{0};
    std::atomic<bool> delivReanchor_{true};
    DeliveryCheck deliv_;   // analysis thread
    std::atomic<double> radioMissing_{0};
    std::atomic<uint64_t> radioMissEvents_{0}, cpuDropEvents_{0}, ringDroppedSeen_{0};
    std::atomic<int64_t> lastRadioMissNs_{0}, lastCpuDropNs_{0}, lastRadioLogNs_{0};
    std::atomic<float> loadPct_{0};
    std::atomic<bool> live_{false};   // the source is a live radio (sampleLoss() is read by the interface without touching src_)
    double loadT_ = 0, loadRx_ = 0, loadSamp_ = 0;   // analysis thread: where the last load measurement started
    std::string reconnectErr_;
    mutable std::mutex noteMu_;
    std::string note_, retuneNote_;     // sourceNote(): the driver's note at the start, at the last retune
    void setNote(const std::string& n, bool retune);
    std::atomic<double> rate_{0};

    std::mutex specMu_;
    SpectrumFrame spec_;

};

// One protocol of a shared receiver as its own mode (mode_tuning.h): runs that receiver with only this protocol's decoder.
// nullptr: every protocol of HF digital and pagers (mesh and marine keep their own settings). Any thread.
void applySubMode(Engine& e, const SubMode* s);

} // namespace dect2
