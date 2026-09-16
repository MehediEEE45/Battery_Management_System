# MQTT -> SQLite Bridge

This small service subscribes to an MQTT topic filter and stores telemetry into a local SQLite database. It also exposes a simple HTTP API to query recent readings.

Usage

1. Copy `.env.example` to `.env` and edit values (broker URL, credentials, port, DB file).

```bash
cd server
cp .env.example .env
# edit .env
npm install
npm start
```

Endpoints

- `GET /api/health` — check service & MQTT connection
- `GET /api/readings/:deviceId?limit=100` — get recent readings for a device
- `POST /api/readings` — add a reading manually (json: `{device_id, topic, payload}`)

Notes

- The bridge expects telemetry topics like `energy/{type}/{deviceId}/telemetry`. It derives `device_id` as `{type}_{deviceId}`.
- Payloads are stored as JSON text. Consider rotating DB and backups for production.

## Deploying so it runs 24/7 (Render — free, no trial/card)

Data only lands in MongoDB while this process is running. Running it on your
own PC means data collection stops the moment the PC sleeps or the terminal
closes. Render's free tier has no trial period and never asks for a card —
the trade-off is it sleeps after ~15 min with no incoming requests, covered
in step 4 below.

1. **Create the service via Blueprint** (uses [`render.yaml`](../render.yaml)
   at the repo root, so most settings are pre-filled):
   - Go to [render.com](https://render.com) → sign in with GitHub → **New +**
     → **Blueprint** → pick `MehediEEE45/Battery_Management_System`.
   - Render reads `render.yaml` and proposes a `bms-mqtt-bridge` web service
     rooted at `server/`, free plan, `npm install` / `npm start`.
   - If you'd rather set it up by hand instead: **New +** → **Web Service** →
     pick the repo → set **Root Directory** to `server`, **Build Command** to
     `npm install`, **Start Command** to `npm start`, **Plan** to `Free`.

2. **Fill in the prompted environment variables** (the Blueprint asks for
   the ones marked `sync: false`; matches [`.env.example`](.env.example)):
   ```
   MQTT_USERNAME=battery
   MQTT_PASSWORD=<your MQTT password>
   MONGO_URI=mongodb+srv://<db_username>:<db_password>@bmscluster.geholqc.mongodb.net/?appName=BMSCluster
   ```
   `MQTT_URL`, `MONGO_DB`, `MONGO_COLLECTION`, `MONGO_TTL_DAYS` are already
   set from `render.yaml`. Don't set `PORT` — Render injects it and
   `index.js` already reads `process.env.PORT`.

3. **Deploy**. Render builds and starts it, then gives you a public URL like
   `https://bms-mqtt-bridge.onrender.com`.

4. **Keep it awake** (free-tier services sleep after ~15 min idle, which
   would pause MQTT collection until the next request wakes it). Set up a
   free external ping every 5–10 min:
   - [cron-job.org](https://cron-job.org) (free, no card) → create a job
     hitting `https://bms-mqtt-bridge.onrender.com/api/status` every 5 min, or
   - [UptimeRobot](https://uptimerobot.com) (free tier, 5 min minimum
     interval) → add an HTTP(s) monitor on the same URL.
   Either keeps the service awake essentially continuously, so MQTT stays
   connected and readings keep saving.

   **Even with pinging, gaps still mean lost data.** A missed ping, a
   redeploy, or platform maintenance restarts the process, and anything the
   ESP32 publishes during that window is gone permanently. Be clear about why
   nothing currently rescues it:

   - **Persistent MQTT session doesn't help.** `render.yaml` sets a fixed
     `MQTT_CLIENT_ID`, so the bridge connects with `clean:false` instead of
     churning a new session per restart. But MQTT only queues messages for an
     offline session at **QoS 1/2**, and the ESP32 publishes at **QoS 0** —
     `client.publish(PUB_TOPIC, buf)` in `mqtt_manager.cpp`, using
     PubSubClient, which supports QoS 0 publishing only. Brokers drop QoS 0
     for disconnected subscribers. The server subscribing at `{ qos: 1 }` is
     a ceiling on delivery, not an upgrade of the publisher's QoS.
   - **The ESP32's EEPROM buffer doesn't help either.** `main.cpp` buffers
     only when `wifiOk && mqttOk` is false — i.e. when the *device* can't
     reach the broker. While this service sleeps, the device's own connection
     is healthy, so it publishes normally and never buffers. The gap is
     invisible to it.

   Two things actually close the gap: **(a)** don't have an outage — run on a
   host that never sleeps (Fly.io's free allowance, or a paid Render
   instance); or **(b)** publish at QoS 1 from firmware, which means swapping
   PubSubClient for a QoS-1-capable client
   ([PsychicMqttClient](https://registry.platformio.org/libraries/elims/PsychicMqttClient)
   or [ESP32MQTTClient](https://registry.platformio.org/libraries/cyijun/ESP32MQTTClient))
   and re-flashing the device.

5. **Point the dashboard at it**: open the deployed
   [public/battery-monitor.html](../public/battery-monitor.html) → **Settings**
   tab → set **Backend URL** to that Render domain, save. The Analytics/
   History/Reports tabs (and CSV/JSON export) read from this URL via
   `/api/mongo/...` endpoints.

6. **Verify**: `curl https://bms-mqtt-bridge.onrender.com/api/status` should
   return `{"mqtt":true,"mongo":true,...}`. Watch the Render service logs —
   you should see `Saved reading for battery_esp32_1 topic=battery/data`
   lines appearing continuously as long as the ESP32 is publishing.

## Deploying to Fly.io (always-on — closes the data-loss gap)

Render's free tier sleeps, and because the ESP32 publishes at QoS 0 (see
above) every sleep window loses readings permanently. Fly.io machines can be
configured never to stop, which removes the outage rather than trying to
cushion it. Config lives in [`fly.toml`](fly.toml) and [`Dockerfile`](Dockerfile).

1. **Install flyctl and sign in**
   ```bash
   # Windows (PowerShell):
   iwr https://fly.io/install.ps1 -useb | iex
   flyctl auth signup     # or: flyctl auth login
   ```
   Fly asks for a card to verify identity; the free allowance isn't charged.

2. **Launch from this directory** (do *not* let it overwrite the existing
   config):
   ```bash
   cd server
   flyctl launch --no-deploy --copy-config
   ```
   App names are globally unique across all Fly users, so if
   `bms-mqtt-bridge` is taken, pick another and update `app =` in `fly.toml`.

3. **Set the secrets** (these are deliberately not in `fly.toml`, which is
   committed to git):
   ```bash
   flyctl secrets set \
     MQTT_USERNAME='battery' \
     MQTT_PASSWORD='<your MQTT password>' \
     MONGO_URI='mongodb+srv://<user>:<pass>@bmscluster.geholqc.mongodb.net/?appName=BMSCluster'
   ```

4. **Deploy**
   ```bash
   flyctl deploy --remote-only
   ```
   `--remote-only` builds on Fly's builders, so local Docker isn't needed.
   **Watch this build**: `sqlite3` is a native module, and if no prebuilt
   binary matches it compiles from source — the `Dockerfile` installs
   `python3/make/g++` for that case, but this first deploy is the real test
   of it. If the build fails on `sqlite3`, the fastest fix is removing it
   (it's redundant — nothing reads the SQLite data back; MongoDB is the
   store).

5. **Verify**
   ```bash
   curl https://<your-app>.fly.dev/api/status
   flyctl logs
   ```
   Expect `{"mqtt":true,"mongo":true,...}` and a steady stream of
   `Saved reading for battery_esp32_1`.

6. **Cut over from Render — important.** While both services run they each
   subscribe to `battery/data` and each insert into the same MongoDB
   collection, so **every reading gets stored twice**. `fly.toml` already
   uses a distinct `MQTT_CLIENT_ID` (`bms-fly-bridge-1`) so the two don't
   fight over one broker session, but the duplicate writes are still real.
   Once Fly is confirmed healthy: suspend or delete the Render service, point
   the dashboard's **Backend URL** at `https://<your-app>.fly.dev`, and
   delete the cron-job.org/UptimeRobot keep-alive ping (Fly doesn't sleep, so
   it serves no purpose).

Note: SQLite writes to ephemeral container storage on Fly and is lost on
restart. That's fine here — the dashboard never reads it; MongoDB is the
real store.

**Other options**: Railway is similar to Render but trial/credit-based, not
permanently free. A paid Render instance also never sleeps if you'd rather
stay on Render.

**Before deploying**: rotate the MQTT and MongoDB passwords if they've ever
been pasted in chat, a screen share, or committed to git history — treat any
credential that left your own machine as potentially exposed.
