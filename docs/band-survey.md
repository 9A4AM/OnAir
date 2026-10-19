# Band survey

A band survey keeps the sweeps of one frequency range over time, so you can see which signals are always there, which parts of the band are quiet, and what changed since the last survey. It lives in the **Spectrum analyser** (the **Band survey** section) and in the command-line tool `dect2sweep`.

## In the app

1. Open the **Spectrum analyser** and run a sweep of the range you care about. With the **every** setting at a few minutes the sweep can run for hours in the background.
2. While **record every sweep into the survey** is on, each sweep adds one row to the survey. Changing the sweep range starts a new survey; the same range carries on.
3. The heat map shows the time down and the frequency across. Two sliders set what counts as a **persistent** signal (above the threshold in at least this share of the sweeps, 5 to 100 %) and the width of a **quiet slot** (10 kHz to 2 MHz).
4. **Persistent signals** and the **quietest slots** are listed under the map.
5. **Load as previous survey...** opens an older survey file and compares: the change of the noise floor and of the occupancy, the signals that are **new** and the ones that are **gone**.

Buttons: **Clear survey**, **Save survey...** (a `.oasurv` file; the survey continues from it when you load it again), **Export occupancy CSV...**, **Export persistent signals CSV...** and **Export picture...** (a PNG of the heat map).

## Command line

```
dect2sweep --from 400 --to 470 --survey band.oasurv --interval 300 --count 48
dect2sweep --survey-report band.oasurv [older.oasurv]
```

`--survey` adds every sweep to the file (a new file when there is none, otherwise it is continued). `--survey-report` prints the persistent signals (frequency, bandwidth, share of time on, levels), the quietest 200 kHz slots, and, when a second older file is given, what appeared and vanished since then.

Status: checked with synthetic sweeps; not yet verified over long runs with a real radio.
