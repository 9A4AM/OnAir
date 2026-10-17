# LBJ (Chinese railway train approach warning)

Receive only. Mode "LBJ", 821.2375 MHz (FM voice channel, 25 kHz). Code: `core/src/lbj/`, panel: `app/lbj/`, tool: `tools/lbjtool.cpp`.

## What is on the air (read off the recordings of issue #153)

| Layer | Finding |
|---|---|
| Modulation | 2-FSK on an FM carrier, 1200 bit/s. Deviation measured about +-4.2 kHz on one locomotive and about +-3.7 kHz on another (the generator and the tests sweep 3.2 to 5 kHz). Polarity is the POCSAG standard: a 1 is the lower frequency. The receiver also takes the reversed polarity (an SDR with swapped I and Q). |
| Transmitter offset | Each locomotive is off frequency by a different amount (we saw up to about 1.5 kHz on HackRF captures); the slicer threshold follows the carrier. |
| Bursts | A burst is 100 to 400 bits (and up) of alternating preamble, the POCSAG sync word 0x7CD215D8, then one 512-bit batch (16 codewords) per sync word; usually one batch. Bursts are a few seconds apart per train and weak: the two samples have 4 to 10 bursts in 50 s. |
| Codewords | Standard POCSAG: BCH(31,21), generator 0x769, even parity; idle word 0x7A89C197. All frames seen use numeric pages. |
| Address | RIC 1234000 (function 1 or 3) in frame 0 (`RIC & 7` = 0 gives the slot). Function 1 is shown as direction "down", 3 as "up" (assumed from public descriptions, not provable from the recordings). |
| Basic message | Three numeric message codewords = 15 characters: `TTTTT` train number (5 digits), a space, `SSS` speed in km/h (3 wide, right aligned), a space, `KKKKK` kilometre post in 0.1 km (5 wide, right aligned). Example off the air: `19533  67   382` = train 19533, 67 km/h, km 38.2; the same train 11 s later `19533  67   385`, and 11 s after that `...387` (0.2 km in 11 s at 67 km/h agrees). |
| Blank message | `----- --- -----` (RIC 1234000, function 1): all fields unused. Seen once; counted as "blank", not listed. |

In the numeric alphabet the codes 10 to 15 are `*` `U` space `-` `)` `(`. A lost field is filled with `?` internally and shown blank.

## How the decoder works

1. `lbj_modem.cpp`: mix the channel to 0 Hz, resample to 48 kHz, 6 kHz 4th-order channel filter, FM discriminator, 1.5 kHz smoothing. A running mean is the slicer threshold. The sync word is searched at all 40 sample phases in both polarities (4 sync bit errors are allowed only after a preamble). The found phase is used to read the batch; the threshold of that batch is the midpoint of the two clusters; the next sync word is searched +-3 samples around its place (clock tracking).
2. `lbj_pocsag.cpp`: BCH check; correction of up to 2 bit errors, and a soft correction (flip the least reliable bits first) that never accepts more than 2 flips in total per word. Pages = address word plus following message words.
3. `lbj_proto.cpp`: fields and plausibility: train digits only, speed at most 400 km/h, digits only in the number fields, RIC must be an LBJ address. A word that needed corrections is flagged (the row is shown in grey). A lost speed or km word leaves that field blank; a lost train number drops the message.

## Not verified

* The long messages (RIC 1234002 / 1234001, or appended to the basic page: locomotive type and number, train class letters, route as GBK text) and the time message (1234008): the layout in `parseDetail` follows public descriptions; none of the recordings contains one. The route text needs iconv (not on Windows: shown as ASCII only there).
* Direction mapping (function 1 / 3).
* Weak bursts: in the second recording (`...170033.cs8`) only one burst decodes (the blank frame in Python analysis); the C++ chain finds the LBJ address in two bursts but not a clean message. That file is weaker than the first.

## Test signal and tests

`makeLbjSynth` (mode code 83) plays a loop of four fake trains. `tests/lbj/`: `test_lbj_proto` (codewords, correction, parsing, a real frame), `test_lbj_rx` (round trip through the whole chain, noise only, polarity, 1.92 Msps), `test_lbj_impair` (SNR, carrier offset, off-centre, deviation, input rates), `test_lbj_real` (optional: skips unless the reporter's recording is in `$LBJ_SAMPLE_DIR` or `~/Downloads`; third-party recordings are not in the repository).

`lbjtool file.cs8 960000 -50500 -v` decodes an IQ file (cs8, cu8 or cf32; the third number is the channel's offset from the file's centre frequency in Hz).

## References and credits

Format facts (what the fields mean, the three message classes) were taken from the READMEs of the LBJ projects by undef-i (LBJ_Console) and liujitong (CR-LBJ-Receiver), both GPL-3.0. No code of either was read or copied; the decoder was written from the POCSAG standard (ITU-R M.584), those descriptions and the recordings above. Credited in `tools/credits_data.tsv`.
