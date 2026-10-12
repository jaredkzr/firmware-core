# Go BTHome Viewer

A single web page that shows the Go's BTHome v2 broadcast on an Android
phone. It scans passively, so the phone never connects and the Go keeps
sleeping between measurements in Portable Sleep.

It shows PM2.5, CO2, temperature, humidity, TVOC index, NOx index and battery,
with sparklines covering the last 4 hours. Readings are kept in the browser's
local storage. Rebroadcasts during connect windows have the same packet id as
the measurement before them, so each measurement is stored once.

## Requirements

- Chrome on Android. iOS browsers have no Web Bluetooth.
- `chrome://flags/#enable-experimental-web-platform-features` enabled. BLE
  advertisement scanning (`requestLEScan`) is still behind this flag.
- A secure origin: `localhost`, HTTPS, or an origin listed under
  `chrome://flags/#unsafely-treat-insecure-origin-as-secure`.
- Bluetooth on, and the Nearby devices (or Location, on older Android)
  permission granted to Chrome.

## Run It

Serve this directory from a computer on the same Wi-Fi network:

```sh
python3 -m http.server 8000 --bind 0.0.0.0 -d products/go/tools/bthome-viewer
```

On the phone:

1. In `chrome://flags`, enable **Experimental Web Platform features**. Add
   `http://<computer-ip>:8000` to **Insecure origins treated as secure** and
   enable it. Relaunch Chrome.
2. Open `http://<computer-ip>:8000`, tap **Start**, and allow the scan.
3. Optionally use **Add to Home screen** in Chrome's menu. The page still loads
   from the computer, so the server has to be running.

Chrome stops scanning when the page goes to the background. Keep it in the
foreground (**Keep screen on** helps) and tap **Start** again after switching
back if the page asks.

Open `#demo` (for example `http://localhost:8000/#demo`) to preview the
layout with made-up data and no Go.

## Files

- `index.html` — the page: scanning, tiles, sparklines and storage
- `bthome.js` — BTHome v2 decoder, matching `main/go_bthome.cpp`
- `bthome.test.mjs` — decoder tests using the firmware's test vectors

Run the decoder tests with:

```sh
node --test products/go/tools/bthome-viewer/bthome.test.mjs
```

## Notes

- The Go has no BTHome object for gas indexes, so it sends TVOC as a uint16
  count (`0x3D`) and NOx as a uint32 count (`0x3E`). The viewer reads them back
  that way. Another BTHome device using those ids would show its counts as TVOC
  and NOx.
- If several BTHome devices are in range, a picker appears, and devices that
  send CO2 are chosen first.
- Air-quality labels: PM2.5 uses US EPA (2024) breakpoints; CO2 uses
  800/1000/1500 ppm. TVOC and NOx use the Sensirion index scales (100 and 1 are
  typical background levels).
