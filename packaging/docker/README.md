# OnAir in Docker, in a browser

The image in `packaging/docker` runs the whole OnAir desktop app on a server without a screen (a NAS, a home server)
and shows it in any browser: picture, sound, mouse and keyboard. It is the same program as the desktop download, nothing is cut
out. The browser part is [Selkies](https://github.com/selkies-project/selkies) from the
[linuxserver.io base image](https://docs.linuxserver.io/images/docker-baseimage-selkies/), which runs OnAir on a virtual display and
streams it as H.264 video with Opus sound.

## Start it

```sh
cd packaging/docker
docker compose up -d --build
```

Then open `https://<server>:3001`. The certificate is self-signed, so the browser warns once. Click into the page once: browsers keep
the sound off until the page has been clicked. The sound button in the sidebar on the left turns it on and off.

Without compose:

```sh
docker build -t onair packaging/docker
docker run -d --name onair -p 3001:3001 --device /dev/bus/usb -v onair-config:/config onair
```

The image installs the release `.deb` of the version named in the `Dockerfile` (`ONAIR_VERSION`). For another release:
`docker build --build-arg ONAIR_VERSION=0.2.6 -t onair packaging/docker`. The in-app update check is off (`ONAIR_NO_UPDATE_CHECK`):
an update installed inside a container is lost when the container is recreated, so build the new version instead.

## Radios

- **USB radios** (HackRF, RTL-SDR, Airspy, ...): pass `/dev/bus/usb` in, as above. At the start the container gives OnAir's user
  access to the radios listed in OnAir's udev rules; only the container's copies of the device files change, nothing on the host.
  Docker copies the device files when the container starts, so **restart the container after plugging a radio in**. The host
  still needs the kernel's DVB driver kept off RTL-SDR dongles (`packaging/linux/onair-rtlsdr-blacklist.conf`).
- **Network radios** (rtl_tcp, SoapyRemote, SpyServer, openHPSDR, ...) need nothing extra.
- **SDRplay** needs SDRplay's own API service, which may not be shipped; it is not in the image.

## Graphics card

Without one, the processor draws the GUI and encodes the stream. With the synthetic 20 Msps signal running and a 1920 x 1040 browser
window, the container used 3.8 cores on an i7-12700K, and 1.8 with its Intel UHD 770 doing that work. Pass the chip's render node in
and name it (the `DRINODE` lines of `compose.yaml`):

```sh
docker run ... --device /dev/dri/renderD128 -e DRINODE=/dev/dri/renderD128 -e DRI_NODE=/dev/dri/renderD128 onair
```

`DRINODE` lets OnAir draw on the chip, `DRI_NODE` lets Selkies encode the video with it (VA-API). With several chips,
`/sys/class/drm/renderD*/device/vendor` tells them apart (0x8086 Intel, 0x1002 AMD). NVIDIA cards work through the NVIDIA Container
Toolkit (`--gpus all`) as the base image describes; that path is not tested with OnAir.

## Settings

| Variable | Default | |
|---|---|---|
| `PUID`, `PGID` | 1000 | owner of the files in `/config` |
| `TZ` | UTC | time zone |
| `CUSTOM_USER`, `PASSWORD` | none | a login for the page. Set one when other computers can reach the port: the page is the whole desktop |
| `LANG` | `en_US.UTF-8` | system language; any other locale is made at the start (OnAir's own language is chosen in the app) |
| `MAX_RES` | `3840x2160` | the largest screen a browser can ask for. The virtual display reserves memory for it (4K: 33 MB; the base image's 15360x8640: 530 MB) |
| `DRINODE`, `DRI_NODE` | none | graphics chip, see above |

The base image has more (`SELKIES_*`, see its documentation). Everything OnAir keeps is in the `/config` volume: settings in
`/config/.config/onair`, recordings and data in `/config/.local/share/onair`, and OnAir's log in `/config/onair.log`, which is
what a bug report needs.

## Notes

- The image is the base image plus OnAir, `libfaad2` (DAB+ HE-AAC), SoapySDR modules and fonts, minus what OnAir does not use
  (the Docker Engine inside the base, the compiler tool chain, `locales-all`, Noto CJK). It is flattened into one layer so that the
  removed files are really gone: about 1.7 GB instead of 2.9 GB. The base image's settings are therefore repeated in the
  `Dockerfile`; check them when the base changes.
- The page is plain HTTP on port 3000 and HTTPS on 3001. Browsers decode the video only on HTTPS (or `localhost`), so put a reverse
  proxy with HTTPS in front of 3000, or use 3001.
- Selkies loads a stand-in `libudev` into the desktop session for its virtual gamepads. With it, libusb finds no USB devices, so
  the start script (`root/defaults/autostart`) starts OnAir without it.
- Tested on x86-64 (Docker 27, Intel UHD 770, HackRF One). The arm64 image is built from the same file and the arm64 `.deb`, but
  has not been run on a radio.
