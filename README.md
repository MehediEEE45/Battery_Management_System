# GitHub Pages deployment

This repository contains a static monitoring dashboard (HTML/JS). The included GitHub Actions workflow deploys the repository root to the `gh-pages` branch whenever you push to `main`.

Quick steps:

1. Create a repository on GitHub and push this code to the `main` branch.
2. The workflow `.github/workflows/gh-pages.yml` will run and publish the site to the `gh-pages` branch.
3. In your repository Settings → Pages, set the Pages source to the `gh-pages` branch (if not auto-selected) and note the site URL.

Example push commands:

```bash
git init
git add .
git commit -m "Initial site"
git remote add origin <git-repo-URL>
git branch -M main
git push -u origin main
# SolarGrid Monitoring Dashboard

Live, lightweight monitoring for ESP32-based telemetry via MQTT with a static web dashboard and a small MQTT→SQLite bridge.

![Demo placeholder](assets/demo.gif)

What this repo contains
- A static web dashboard (HTML/JS) to visualize telemetry (see `public/` and root HTML files).
- An ESP32 example that publishes/subscribes via MQTT: [esp32-mqtt/esp32_mqtt.ino](esp32-mqtt/esp32_mqtt.ino).
- A small MQTT-to-SQLite bridge and HTTP API: [server/index.js](server/index.js) (see [server/README.md](server/README.md)).
- A GitHub Actions workflow to publish the site to GitHub Pages from `main`.

Highlights
- Real-time dashboard for energy/solar/wind telemetry
- Simple ESP32 example using TLS MQTT (PubSubClient)
- Local bridge that persists telemetry to SQLite and exposes a REST API
- Quick to run locally or deploy via GitHub Pages

Quickstart (3 steps)

1. Flash the ESP32

	- Edit [esp32-mqtt/esp32_mqtt.ino](esp32-mqtt/esp32_mqtt.ino) and set `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_USER`, `MQTT_PASSWORD`.
	- Build & upload using Arduino IDE or PlatformIO.

2. Start the MQTT→SQLite bridge

```bash
cd server
cp .env.example .env
# edit .env to point to your broker and set credentials
npm install
npm start
```

3. Open the dashboard

- For a quick test open `public/index.html` in a browser (or deploy via GitHub Pages — the repo contains a workflow to publish the root to `gh-pages`).

Architecture

- ESP32 (publishes telemetry) → MQTT broker (HiveMQ / local broker) → MQTT→SQLite bridge (`server/`) → Static dashboard (`public/` / GitHub Pages)

Key files
- Firmware: [esp32-mqtt/esp32_mqtt.ino](esp32-mqtt/esp32_mqtt.ino)
- Web WSS demo: [esp32-mqtt/web_mqtt_example.html](esp32-mqtt/web_mqtt_example.html)
- Bridge: [server/index.js](server/index.js)
- Server README: [server/README.md](server/README.md)
- Pages deployment notes: [README.md](README.md)

Usage and examples
- Topics used: `esp32/test` (device telemetry) and `esp32/commands` (control commands). The bridge also expects telemetry topics like `energy/{type}/{deviceId}/telemetry` by default — see [server/README.md](server/README.md).
- Example payload (JSON):

```json
{ "device_id": "panel_01", "voltage": 12.4, "current": 1.8, "power": 22.3, "temperture": 25 ,"ts": 167" }
```

Security notes
- The ESP32 example uses `setInsecure()` for simplicity in development. Replace with proper CA verification for production.
- Rotate MQTT credentials and limit broker access.
- For the bridge, consider DB rotation/backup for long-term storage.

Visuals & assets to add
- Demo GIF (5–8s) showing live data on the dashboard
- Screenshots: dashboard, ESP32 serial log, bridge API response
- Architecture SVG

Polish & metadata suggestions
- Add badges: `build/deploy`, `license` (MIT), `platform: ESP32`, `node`.
- Add `LICENSE` (MIT recommended) and `CONTRIBUTING.md`.

Contributing

Contributions welcome — open an issue or submit a PR. For major changes, please open an issue first to discuss the design.

License

This project is suitable for permissive distribution — add a `LICENSE` file (MIT recommended).


Contact / Demo

Supervisor: Arif Ahammad — Assistant Professor

Live demo (Netlify): [https://power-monitor.netlify.app/](https://iotpowerhub.netlify.app/)

Open an issue to report problems or request features. This project is hosted publicly on Netlify (link above). The repository also includes a GitHub Actions workflow that can publish the static site to the `gh-pages` branch when you push to `main`; if you prefer GitHub Pages, set the Pages source in repository Settings to the `gh-pages` branch to enable that URL as well.
---

If you want, I can now:
- generate a ready-to-use architecture SVG and example GIF (C),
- or produce an HTML-based GitHub Pages post using this content (B).
Choose which and I'll continue.
