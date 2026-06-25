# LED Flight Tracker

A WiFi-connected LED matrix display that shows aircraft flying overhead in real
time — callsign, route (origin ▸ destination), and speed — on a HUB75 RGB panel
driven by an ESP32-S3.

Flight positions come from the free [OpenSky Network](https://opensky-network.org)
API, and origin/destination routes are looked up from a free public data source
(no scraping, no extra account needed).

![Flight tracker showing a flight overhead](docs/images/hero.jpg)
<!-- TODO: replace with a photo of the assembled tracker displaying a real flight -->

---

## Setup Guide

This guide walks you through everything needed to get your own flight tracker
running, from creating the required accounts to entering your details on the
device. No coding required for the configuration steps — it's all done through a
web page the tracker hosts itself.

### What you'll need

- An assembled flight tracker (ESP32-S3 + HUB75 LED panel) with the firmware
  flashed — see [Flashing the firmware](#flashing-the-firmware) below if you're
  starting from scratch.
- A phone or laptop with WiFi (to connect to the tracker during setup).
- Your home **WiFi network name (SSID) and password**. Note: the tracker
  connects to **2.4 GHz** WiFi only (it cannot use 5 GHz networks).
- A free **OpenSky Network** account (we'll create this in Step 1).

---

### Step 1 — Create a free OpenSky Network account

The tracker uses OpenSky's API to find out which aircraft are currently in the
air near you. A free account gives you a daily allowance of API requests that's
more than enough for a single tracker.

1. Go to **<https://opensky-network.org>** and click **Sign up** (top right).
2. Fill in a username, email, and password, then verify your email.
3. Log in, then open your **Account** page.

![OpenSky sign-up page](docs/images/opensky-signup.png)
<!-- TODO: screenshot of the OpenSky registration page -->

---

### Step 2 — Generate your API client credentials

OpenSky uses an **API Client ID** and **Client Secret** (OAuth2) to authenticate
the tracker. You generate these once and paste them into the device.

1. On your OpenSky **Account** page, find the **API Clients** section.
2. Create a new API client. Give it any name you like (e.g. `flight-tracker`).
3. OpenSky will show you a **Client ID** and a **Client Secret**.
4. **Copy both somewhere safe now** — the secret is usually only shown once. If
   you lose it, you can delete the client and create a new one.

![OpenSky API client credentials](docs/images/opensky-credentials.png)
<!-- TODO: screenshot of the API Clients section showing where Client ID / Secret appear (blur out the real values) -->

> 💡 You'll paste the **Client ID** and **Client Secret** into the tracker in
> Step 4.

---

### Step 3 — Connect to the tracker's WiFi network

When the tracker has no saved configuration (e.g. first power-on), it starts its
own temporary WiFi network so you can connect and configure it.

1. Power on the tracker. After a moment the LED panel shows a **setup / config
   mode** message.
2. On your phone or laptop, open the WiFi settings and connect to:

   | Setting  | Value          |
   | -------- | -------------- |
   | Network  | `FlightTracker` |
   | Password | `12345678`     |

3. Once connected, a setup page should open automatically. If it doesn't, open a
   browser and go to **<http://192.168.4.1>**.

![Tracker showing setup mode on the LED panel](docs/images/device-setup-mode.jpg)
<!-- TODO: photo of the LED panel showing the setup/config-mode message -->

> ℹ️ Your phone may warn that this WiFi network "has no internet" — that's
> expected, stay connected to it for the setup.

---

### Step 4 — Enter your WiFi and OpenSky details

On the setup page you'll see a short form. Fill in all four fields:

| Field                    | What to enter                                              |
| ------------------------ | ---------------------------------------------------------- |
| **WiFi Network Name (SSID)** | Your home 2.4 GHz WiFi network name                    |
| **WiFi Password**        | Your home WiFi password                                    |
| **OpenSky Client ID**    | The Client ID from Step 2                                  |
| **OpenSky Client Secret**| The Client Secret from Step 2                             |

Then tap **Save & Connect**.

![Setup form for WiFi and OpenSky credentials](docs/images/setup-form.png)
<!-- TODO: screenshot of the device's setup web form (the page served at 192.168.4.1) -->

The tracker will then:

1. Save your details and **reboot**.
2. Connect to your home WiFi.
3. Validate your OpenSky credentials.

If something is wrong (WiFi won't connect, or the OpenSky credentials are
rejected), the panel shows a brief error and the tracker **wipes the bad config
and returns to setup mode** so you can try again from Step 3.

> ⚠️ Double-check your WiFi password and OpenSky credentials if it keeps
> returning to setup mode — these are the two most common mistakes.

---

### Step 5 — Find your tracker on the network

Once connected, the panel briefly scrolls the address where you can reach the
tracker's **settings page**:

- **<http://flighttracker.local/settings>** (works on most phones and Macs), or
- **`http://<IP-address>/settings`** using the IP shown on the panel (e.g.
  `http://192.168.1.42/settings`).

![Panel showing the settings address](docs/images/device-settings-address.jpg)
<!-- TODO: photo of the LED panel scrolling the "Settings at: flighttracker.local" message -->

You're now up and running — when aircraft are overhead they'll appear on the
display automatically. 🎉

---

## Customising your tracker (Settings page)

Open the settings page (Step 5) any time the tracker is on your WiFi to adjust
how it looks and what area it watches. Most changes apply instantly; changing
the tracking area reboots the device.

![Settings page](docs/images/settings-page.png)
<!-- TODO: screenshot of the full settings page (flighttracker.local/settings) -->

The settings page includes:

- **Device info** — current WiFi, IP address, remaining OpenSky API credits,
  uptime, and how many flights are currently overhead.
- **Brightness** — slider from 1 (dimmest) to 255 (brightest).
- **Colours** — pick the colour for the route, callsign, speed, and the flight
  counter independently.
- **No Flights Display** — what to show when the sky is empty: a "No flights"
  message, a **clock**, or an animated **bonsai tree**.
- **Tracking Area** — the geographic box the tracker watches.

### Setting your tracking area

The tracker watches a rectangular area (a "bounding box") around your location.
The default is set to the greater Sydney region — you'll want to change this to
your own area.

On the settings page, scroll to **Tracking Area** and either:

- **Click two points on the map** to draw the rectangle, or
- Type the **South / North / West / East** coordinates directly.

Tap **Save & Reboot** to apply. Keep the box reasonably small (your local area)
so the display focuses on aircraft genuinely near you.

![Tracking area map selector](docs/images/settings-tracking-area.png)
<!-- TODO: screenshot of the map / bounding-box selector on the settings page -->

---

## Re-configuring or resetting

- **Change WiFi or OpenSky credentials later:** Hold the button on the device
  while powering on (keep it held for ~3 seconds). The tracker re-enters setup
  mode and broadcasts the `FlightTracker` network again — then repeat from
  Step 3.
- **Full reset:** On the settings page, scroll to the bottom and tap
  **Reset Device**. This erases all settings and reboots into setup mode.

![Reset button on the settings page](docs/images/settings-reset.png)
<!-- TODO: screenshot of the Reset Device button at the bottom of the settings page -->

---

## Troubleshooting

| Problem | Likely cause / fix |
| ------- | ------------------ |
| Keeps returning to setup mode | Wrong WiFi password, or wrong OpenSky Client ID/Secret. Re-check both. |
| Can't connect to `FlightTracker` WiFi | Make sure the panel is in setup mode; the password is `12345678`. |
| Setup page won't load | Browse to `http://192.168.4.1` manually while connected to the tracker's WiFi. |
| `flighttracker.local` doesn't work | Use the IP address shown on the panel instead (`http://<IP>/settings`). |
| Won't join home WiFi | The tracker only supports **2.4 GHz** networks, not 5 GHz. |
| No flights ever show | Check your tracking area covers somewhere with air traffic, and that API credits remain (shown on the settings page). |

> 🌙 **Quiet hours:** the tracker pauses polling between **11 pm and 6 am** to
> save API credits, so the display stays idle overnight by design.

---

## Flashing the firmware

If you're building a tracker from scratch (rather than configuring one that's
already flashed), you'll need to build and flash the firmware once.

This is an [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) project
targeting the **ESP32-S3**, built with ESP-IDF **v5.4**.

```bash
# From the project root, with ESP-IDF v5.4 installed:
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash        # e.g. /dev/ttyACM0 on Linux, COMx on Windows
idf.py -p <PORT> monitor      # optional: watch the serial log
```

> The HUB75 panel wiring (pin mapping) is hardware-specific. If you're using a
> different panel or wiring, adjust the display configuration before flashing.

Once flashed, power-cycle the device and continue from
[Step 3](#step-3--connect-to-the-trackers-wifi-network).

---

## Images

Screenshot/photo placeholders referenced above live in [`docs/images/`](docs/images/).
See [docs/images/README.md](docs/images/README.md) for the list of shots still
needed.
