require('dotenv').config();
const fs = require('fs');
const path = require('path');
const mqtt = require('mqtt');
const express = require('express');
const bodyParser = require('body-parser');
const sqlite3 = require('sqlite3').verbose();
const { MongoClient, ServerApiVersion } = require('mongodb');

const MQTT_URL = process.env.MQTT_URL || 'mqtt://localhost:1883';
const MQTT_USERNAME = process.env.MQTT_USERNAME || '';
const MQTT_PASSWORD = process.env.MQTT_PASSWORD || '';
// Subscribe to both energy topics and smartpower topics (ESP32 publishes to smartpower/+/data)
const MQTT_TOPIC_FILTER = process.env.MQTT_TOPIC_FILTER || 'energy/+/+/telemetry';
const MQTT_TOPIC_FILTER_2 = process.env.MQTT_TOPIC_FILTER_2 || 'smartpower/+/data';
const MQTT_TOPIC_FILTER_3 = process.env.MQTT_TOPIC_FILTER_3 || 'battery/data';
const PORT = parseInt(process.env.PORT || '3000', 10);
const DB_FILE = process.env.DB_FILE || 'telemetry.db';
const MONGO_URI = process.env.MONGO_URI || '';
const MONGO_DB = process.env.MONGO_DB || 'battery_monitor';
const MONGO_COLLECTION = process.env.MONGO_COLLECTION || 'telemetry';
const MONGO_TTL_DAYS = process.env.MONGO_TTL_DAYS ? parseInt(process.env.MONGO_TTL_DAYS, 10) : 0;

// Ensure DB exists
const dbPath = path.resolve(__dirname, DB_FILE);
const db = new sqlite3.Database(dbPath);

db.serialize(() => {
  db.run(`CREATE TABLE IF NOT EXISTS readings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    topic TEXT NOT NULL,
    payload TEXT NOT NULL,
    ts INTEGER NOT NULL
  )`);
});

function saveReading(deviceId, topic, payload) {
  const ts = Date.now();
  const stmt = db.prepare('INSERT INTO readings (device_id, topic, payload, ts) VALUES (?, ?, ?, ?)');
  stmt.run(deviceId, topic, JSON.stringify(payload), ts, function (err) {
    if (err) console.error('DB insert error', err);
    stmt.finalize();
  });
}

// Optional: MongoDB client and collection
let mongoClient = null;
let mongoCol = null;
async function initMongo() {
  if (!MONGO_URI) return;
  try {
    mongoClient = new MongoClient(MONGO_URI, {
      serverApi: {
        version: ServerApiVersion.v1,
        strict: true,
        deprecationErrors: true,
      },
      connectTimeoutMS: 5000,
    });
    await mongoClient.connect();
    // ping to verify connection
    await mongoClient.db('admin').command({ ping: 1 });
    const db = mongoClient.db(MONGO_DB);
    mongoCol = db.collection(MONGO_COLLECTION);
    console.log('Connected to MongoDB', MONGO_DB, MONGO_COLLECTION);

    // Ensure indexes: device_id + ts descending for queries
    await mongoCol.createIndex({ device_id: 1, ts: -1 });

    // Optional TTL index on `ts` (convert days to seconds)
    if (MONGO_TTL_DAYS > 0) {
      const seconds = MONGO_TTL_DAYS * 24 * 60 * 60;
      // create a TTL index on 'ts_date' which will store a proper Date
      await mongoCol.createIndex({ ts_date: 1 }, { expireAfterSeconds: seconds });
      console.log('Created TTL index to expire documents after', MONGO_TTL_DAYS, 'days');
    }
  } catch (e) {
    console.error('MongoDB init error', e);
    try { await mongoClient.close(); } catch (e2) {}
    mongoClient = null;
    mongoCol = null;
  }
}

// Connect to MQTT
// MQTT connection options with reconnection logic and clientId
const mqttOptions = {
  reconnectPeriod: 5000,
  clientId: process.env.MQTT_CLIENT_ID || `node-bridge-${Math.random().toString(16).slice(2, 8)}`,
  clean: true,
};
if (MQTT_USERNAME) mqttOptions.username = MQTT_USERNAME;
if (MQTT_PASSWORD) mqttOptions.password = MQTT_PASSWORD;

console.log('Connecting to MQTT broker:', MQTT_URL, 'clientId=', mqttOptions.clientId);
const client = mqtt.connect(MQTT_URL, mqttOptions);

client.on('connect', () => {
  console.log('Connected to MQTT broker');
  // Subscribe to all configured topic filters
  [MQTT_TOPIC_FILTER, MQTT_TOPIC_FILTER_2, MQTT_TOPIC_FILTER_3].forEach(topic => {
    if (topic) {
      client.subscribe(topic, { qos: 1 }, (err) => {
        if (err) console.error('Subscribe error', err);
        else console.log('Subscribed to', topic);
      });
    }
  });
});

client.on('error', (err) => {
  console.error('MQTT error', err);
});

client.on('message', async (topic, message) => {
  let payload = null;
  const raw = message.toString();
  try { payload = JSON.parse(raw); } catch { payload = raw; }

  // Extract device id from topic
  const parts = topic.split('/');
  let deviceId = topic;
  if (parts[0] === 'battery') {
    // ESP32 BMS publishes to battery/data
    deviceId = 'battery_esp32_1';
  } else if (parts.length >= 3) {
    deviceId = `${parts[1]}_${parts[2]}`;
  }

  // Basic validation and normalization
  const doc = {
    device_id: deviceId,
    topic,
    raw: raw,
    ts: Date.now(),
  };

  if (payload && typeof payload === 'object') {
    doc.device_id = payload.device_id || deviceId;
    doc.voltage = (payload.bus_V ?? payload.voltage) != null ? Number(payload.bus_V ?? payload.voltage) : null;
    doc.current = (payload.current_A ?? payload.current) != null ? Number(payload.current_A ?? payload.current) : null;
    doc.power = (payload.power_W ?? payload.power) != null ? Number(payload.power_W ?? payload.power) : null;
    doc.temperature = payload.temperature != null ? Number(payload.temperature) : null;
    doc.soc_percent = payload.soc_percent != null ? Number(payload.soc_percent) : null;
    doc.soh_percent = payload.soh_percent != null ? Number(payload.soh_percent) : null;
    doc.uptime_ms = payload.uptime_ms ?? null;
    doc.payload = payload;
  } else {
    doc.payload = raw;
  }

  // Save to sqlite for compatibility
  saveReading(doc.device_id, topic, doc.payload);

  // Save to MongoDB if enabled
  if (mongoCol) {
    try {
      // add a Date field for TTL index if configured
      doc.ts_date = new Date(doc.ts);
      await mongoCol.insertOne(doc);
    } catch (e) {
      console.error('Mongo insert failed', e);
    }
  }

  console.log(`Saved reading for ${doc.device_id} topic=${topic}`);

  // Broadcast to connected WebSocket clients
  try {
    const wsMsg = JSON.stringify({ topic, deviceId: doc.device_id, payload: doc.payload, ts: doc.ts });
    wss.clients.forEach((c) => {
      if (c.readyState === WebSocket.OPEN) c.send(wsMsg);
    });
  } catch (e) {
    console.error('WS broadcast error', e);
  }
});

// Simple HTTP API
const app = express();
app.use(bodyParser.json());

// CORS — allow Netlify and local dev frontends
app.use((req, res, next) => {
  const allowedOrigins = ['https://iotpowerhub.netlify.app', 'https://power-monitor.netlify.app', 'http://localhost:3000', 'http://127.0.0.1:3000'];
  const origin = req.headers.origin;
  if (origin && (allowedOrigins.includes(origin) || origin.startsWith('http://192.168.') || origin.startsWith('http://localhost'))) {
    res.setHeader('Access-Control-Allow-Origin', origin);
  }
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, DELETE, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  if (req.method === 'OPTIONS') return res.sendStatus(204);
  next();
});

// Serve static files from public directory
app.use(express.static(path.join(__dirname, '../public')));

// Get recent readings for a device
app.get('/api/readings/:deviceId', (req, res) => {
  const deviceId = req.params.deviceId;
  const limit = parseInt(req.query.limit || '100', 10);
  db.all('SELECT id, device_id, topic, payload, ts FROM readings WHERE device_id = ? ORDER BY ts DESC LIMIT ?', [deviceId, limit], (err, rows) => {
    if (err) return res.status(500).json({ error: 'DB error' });
    const parsed = rows.map(r => ({ id: r.id, device_id: r.device_id, topic: r.topic, payload: (() => { try { return JSON.parse(r.payload); } catch { return r.payload; } })(), ts: r.ts }));
    res.json(parsed);
  });
});

// Health
app.get('/api/health', (req, res) => res.json({ ok: true, mqtt: client.connected }));

// Post a reading (optional - allows direct HTTP ingestion)
app.post('/api/readings', (req, res) => {
  const { device_id, topic, payload } = req.body || {};
  if (!device_id || !topic || !payload) return res.status(400).json({ error: 'Missing fields' });
  saveReading(device_id, topic, payload);
  res.json({ success: true });
});

// ===== MongoDB Read Endpoints (for website to fetch historical data) =====

// Get recent readings from MongoDB
app.get('/api/mongo/readings/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const limit = parseInt(req.query.limit || '100', 10);
    const docs = await mongoCol
      .find({ device_id: deviceId })
      .sort({ ts: -1 })
      .limit(limit)
      .toArray();
    res.json(docs.reverse()); // reverse to get chronological order
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Get readings from MongoDB within a date range
app.get('/api/mongo/readings/:deviceId/range', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const from = req.query.from ? new Date(req.query.from) : new Date(Date.now() - 24*60*60*1000);
    const to = req.query.to ? new Date(req.query.to) : new Date();
    const docs = await mongoCol
      .find({ device_id: deviceId, ts: { $gte: from.getTime(), $lte: to.getTime() } })
      .sort({ ts: 1 })
      .toArray();
    res.json(docs);
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Get statistics (avg, min, max) from MongoDB for the last N hours
app.get('/api/mongo/stats/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const hours = parseInt(req.query.hours || '24', 10);
    const cutoffTime = Date.now() - (hours * 60 * 60 * 1000);
    
    const docs = await mongoCol
      .find({ device_id: deviceId, ts: { $gte: cutoffTime } })
      .toArray();
    
    if (docs.length === 0) {
      return res.json({ device_id: deviceId, count: 0, voltage: null, current: null, power: null, soc: null });
    }

    const keyAlts = { bus_V: 'voltage', current_A: 'current', power_W: 'power', temperature: 'temp_c', soc_percent: 'soc', soh_percent: 'soh' };
    const getNumbers = (key) => docs.map(d => {
      if (!d.payload) return null;
      let val = d.payload[key];
      if (val == null && keyAlts[key]) val = d.payload[keyAlts[key]];
      return (val != null && !isNaN(val)) ? Number(val) : null;
    }).filter(v => v !== null);

    const calcStats = (arr) => {
      if (arr.length === 0) return null;
      const avg = arr.reduce((a, b) => a + b, 0) / arr.length;
      const min = Math.min(...arr);
      const max = Math.max(...arr);
      return { avg: parseFloat(avg.toFixed(2)), min: parseFloat(min.toFixed(2)), max: parseFloat(max.toFixed(2)) };
    };

    res.json({
      device_id: deviceId,
      count: docs.length,
      hours,
      voltage: calcStats(getNumbers('bus_V')),
      current: calcStats(getNumbers('current_A')),
      power: calcStats(getNumbers('power_W')),
      temperature: calcStats(getNumbers('temperature')),
      soc: calcStats(getNumbers('soc_percent')),
      soh: calcStats(getNumbers('soh_percent')),
      ts_range: { from: docs[0].ts, to: docs[docs.length - 1].ts }
    });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// ===== 30-Day Analysis Endpoints =====

// Get 30-day readings
app.get('/api/mongo/readings/30days/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const days = parseInt(req.query.days || '30', 10);
    const cutoffTime = Date.now() - (days * 24 * 60 * 60 * 1000);
    
    const docs = await mongoCol
      .find({ device_id: deviceId, ts: { $gte: cutoffTime } })
      .sort({ ts: 1 })
      .toArray();
    
    res.json({ device_id: deviceId, days, count: docs.length, data: docs });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Get 30-day stats (aggregate)
app.get('/api/mongo/stats/30days/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const days = parseInt(req.query.days || '30', 10);
    const cutoffTime = Date.now() - (days * 24 * 60 * 60 * 1000);
    
    const docs = await mongoCol
      .find({ device_id: deviceId, ts: { $gte: cutoffTime } })
      .toArray();
    
    if (docs.length === 0) {
      return res.json({ device_id: deviceId, days, count: 0, stats: null });
    }

    const keyAlts2 = { bus_V: 'voltage', current_A: 'current', power_W: 'power', temperature: 'temp_c', soc_percent: 'soc', soh_percent: 'soh' };
    const getNumbers = (key) => docs.map(d => {
      if (!d.payload) return null;
      let val = d.payload[key];
      if (val == null && keyAlts2[key]) val = d.payload[keyAlts2[key]];
      return (val != null && !isNaN(val)) ? Number(val) : null;
    }).filter(v => v !== null);

    const calcStats = (arr) => {
      if (arr.length === 0) return null;
      arr.sort((a, b) => a - b);
      const avg = arr.reduce((a, b) => a + b, 0) / arr.length;
      const min = arr[0];
      const max = arr[arr.length - 1];
      const median = arr.length % 2 === 0 ? (arr[arr.length/2 - 1] + arr[arr.length/2]) / 2 : arr[Math.floor(arr.length/2)];
      return { avg: parseFloat(avg.toFixed(2)), min: parseFloat(min.toFixed(2)), max: parseFloat(max.toFixed(2)), median: parseFloat(median.toFixed(2)), count: arr.length };
    };

    const voltages = getNumbers('bus_V');
    const currents = getNumbers('current_A');
    const powers = getNumbers('power_W');
    const temperatures = getNumbers('temperature');
    const socs = getNumbers('soc_percent');
    const sohs = getNumbers('soh_percent');
    
    // Calculate energy (kWh) - assuming 5 second intervals
    const energyKwh = (powers.reduce((a, b) => a + b, 0) * 5 / 3600 / 1000).toFixed(2);

    res.json({
      device_id: deviceId,
      days,
      total_records: docs.length,
      timestamp_range: { from: new Date(docs[0].ts), to: new Date(docs[docs.length-1].ts) },
      stats: {
        voltage: calcStats(voltages),
        current: calcStats(currents),
        power: calcStats(powers),
        temperature: calcStats(temperatures),
        energy_kwh: energyKwh,
        soc: calcStats(socs),
        soh: calcStats(sohs)
      }
    });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Get daily aggregates (for trend chart)
app.get('/api/mongo/trends/30days/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const days = parseInt(req.query.days || '30', 10);
    const cutoffTime = Date.now() - (days * 24 * 60 * 60 * 1000);
    
    const docs = await mongoCol
      .find({ device_id: deviceId, ts: { $gte: cutoffTime } })
      .sort({ ts: 1 })
      .toArray();
    
    if (docs.length === 0) return res.json({ device_id: deviceId, trends: [] });

    // Group by day
    const byDay = {};
    docs.forEach(doc => {
      const date = new Date(doc.ts).toISOString().split('T')[0]; // YYYY-MM-DD
      if (!byDay[date]) byDay[date] = [];
      byDay[date].push(doc);
    });

    const trends = Object.entries(byDay).map(([date, dayDocs]) => {
      const voltages = dayDocs.map(d => d.payload?.bus_V).filter(v => v !== null && v !== undefined);
      const currents = dayDocs.map(d => d.payload?.current_A).filter(v => v !== null && v !== undefined);
      const powers = dayDocs.map(d => d.payload?.power_W).filter(v => v !== null && v !== undefined);
      const temperatures = dayDocs.map(d => d.payload?.temperature).filter(v => v !== null && v !== undefined);
      const socs = dayDocs.map(d => d.payload?.soc_percent).filter(v => v !== null && v !== undefined);
      
      const avg = (arr) => arr.length ? (arr.reduce((a,b) => a+b, 0) / arr.length).toFixed(2) : null;
      const energyKwh = powers.length ? (powers.reduce((a,b) => a+b, 0) * 5 / 3600 / 1000).toFixed(2) : 0;
      
      return {
        date,
        count: dayDocs.length,
        voltage_avg: avg(voltages),
        current_avg: avg(currents),
        power_avg: avg(powers),
        temperature_avg: avg(temperatures),
        energy_kwh: energyKwh,
        soc_avg: avg(socs)
      };
    });

    res.json({ device_id: deviceId, days, total_days: trends.length, trends });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Export as CSV
app.get('/api/mongo/export/csv/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const days = parseInt(req.query.days || '30', 10);
    const cutoffTime = Date.now() - (days * 24 * 60 * 60 * 1000);
    
    const docs = await mongoCol
      .find({ device_id: deviceId, ts: { $gte: cutoffTime } })
      .sort({ ts: 1 })
      .toArray();
    
    if (docs.length === 0) return res.status(404).json({ error: 'No data found' });

    // Create CSV
    let csv = 'Timestamp,Date,Voltage (V),Current (A),Power (W),Temperature (C),SoC (%),SoH (%),Uptime (ms)\n';
    docs.forEach(doc => {
      const p = doc.payload || {};
      const date = new Date(doc.ts);
      const voltage = p.bus_V ?? p.voltage ?? '';
      const current = p.current_A ?? p.current ?? '';
      const power = p.power_W ?? p.power ?? '';
      const temperature = p.temperature ?? '';
      const soc = p.soc_percent ?? '';
      const soh = p.soh_percent ?? '';
      const uptime = p.uptime_ms ?? '';
      csv += `${doc.ts},"${date.toISOString()}",${voltage},${current},${power},${temperature},${soc},${soh},${uptime}\n`;
    });

    res.setHeader('Content-Type', 'text/csv');
    res.setHeader('Content-Disposition', `attachment; filename="battery_data_${deviceId}_${days}days.csv"`);
    res.send(csv);
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Export as JSON
app.get('/api/mongo/export/json/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const days = parseInt(req.query.days || '30', 10);
    const cutoffTime = Date.now() - (days * 24 * 60 * 60 * 1000);
    
    const docs = await mongoCol
      .find({ device_id: deviceId, ts: { $gte: cutoffTime } })
      .sort({ ts: 1 })
      .toArray();
    
    res.setHeader('Content-Type', 'application/json');
    res.setHeader('Content-Disposition', `attachment; filename="battery_data_${deviceId}_${days}days.json"`);
    res.json({
      device_id: deviceId,
      days,
      total_records: docs.length,
      exported_at: new Date().toISOString(),
      data: docs.map(d => ({
        timestamp: d.ts,
        date: new Date(d.ts).toISOString(),
        voltage: d.payload?.bus_V ?? d.payload?.voltage,
        current: d.payload?.current_A ?? d.payload?.current,
        power: d.payload?.power_W ?? d.payload?.power,
        temperature: d.payload?.temperature ?? null,
        soc: d.payload?.soc_percent,
        soh: d.payload?.soh_percent,
        uptime_ms: d.payload?.uptime_ms
      }))
    });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// ===== Write / Delete / Command Endpoints =====

// POST: Insert a manual reading into MongoDB
app.post('/api/mongo/readings/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const { payload } = req.body || {};
    if (!payload || typeof payload !== 'object') return res.status(400).json({ error: 'Missing payload object' });
    const doc = {
      device_id: deviceId,
      topic: 'manual/insert',
      ts: Date.now(),
      ts_date: new Date(),
      voltage: (payload.bus_V ?? payload.voltage) != null ? Number(payload.bus_V ?? payload.voltage) : null,
      current: (payload.current_A ?? payload.current) != null ? Number(payload.current_A ?? payload.current) : null,
      power: (payload.power_W ?? payload.power) != null ? Number(payload.power_W ?? payload.power) : null,
      temperature: payload.temperature != null ? Number(payload.temperature) : null,
      soc_percent: payload.soc_percent != null ? Number(payload.soc_percent) : null,
      soh_percent: payload.soh_percent != null ? Number(payload.soh_percent) : null,
      uptime_ms: payload.uptime_ms ?? null,
      payload
    };
    const result = await mongoCol.insertOne(doc);
    res.json({ success: true, insertedId: result.insertedId });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// POST: Bulk insert readings (for syncing localStorage data)
app.post('/api/mongo/sync/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const { readings } = req.body || {};
    if (!Array.isArray(readings) || readings.length === 0) return res.status(400).json({ error: 'readings must be a non-empty array' });
    const docs = readings.map(r => {
      const p = r.payload || r;
      return {
        device_id: deviceId,
        topic: 'sync/browser',
        ts: r.timestamp ? new Date(r.timestamp).getTime() : Date.now(),
        ts_date: r.timestamp ? new Date(r.timestamp) : new Date(),
        voltage: (p.bus_V ?? p.voltage) != null ? Number(p.bus_V ?? p.voltage) : null,
        current: (p.current_A ?? p.current) != null ? Number(p.current_A ?? p.current) : null,
        power: (p.power_W ?? p.power) != null ? Number(p.power_W ?? p.power) : null,
        temperature: p.temperature != null ? Number(p.temperature) : null,
        soc_percent: p.soc_percent != null ? Number(p.soc_percent) : null,
        soh_percent: p.soh_percent != null ? Number(p.soh_percent) : null,
        uptime_ms: p.uptime_ms ?? null,
        payload: p
      };
    });
    const result = await mongoCol.insertMany(docs);
    res.json({ success: true, insertedCount: result.insertedCount });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// DELETE: Purge all data for a device
app.delete('/api/mongo/readings/:deviceId', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const result = await mongoCol.deleteMany({ device_id: deviceId });
    res.json({ success: true, deletedCount: result.deletedCount });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// DELETE: Purge data for a device within a date range
app.delete('/api/mongo/readings/:deviceId/range', async (req, res) => {
  if (!mongoCol) return res.status(503).json({ error: 'MongoDB not connected' });
  try {
    const deviceId = req.params.deviceId;
    const from = req.query.from ? new Date(req.query.from).getTime() : 0;
    const to = req.query.to ? new Date(req.query.to).getTime() : Date.now();
    const result = await mongoCol.deleteMany({ device_id: deviceId, ts: { $gte: from, $lte: to } });
    res.json({ success: true, deletedCount: result.deletedCount });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// POST: Send a command to ESP32 via MQTT (publish to battery/recieve)
app.post('/api/command/:deviceId', (req, res) => {
  if (!client.connected) return res.status(503).json({ error: 'MQTT not connected' });
  try {
    const { command, value } = req.body || {};
    if (!command) return res.status(400).json({ error: 'Missing command field' });
    const payload = JSON.stringify({ command, value: value ?? null, ts: Date.now(), from: 'web-dashboard' });
    const topic = process.env.MQTT_COMMAND_TOPIC || 'battery/recieve';
    client.publish(topic, payload, { qos: 1 }, (err) => {
      if (err) return res.status(500).json({ error: 'Publish failed: ' + err.message });
      console.log(`Command sent to ${topic}: ${payload}`);
      res.json({ success: true, topic, payload: JSON.parse(payload) });
    });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// GET: Server status (MongoDB + MQTT + counts)
app.get('/api/status', async (req, res) => {
  const status = {
    mqtt: client.connected,
    mongo: !!mongoCol,
    uptime: process.uptime(),
    ts: Date.now()
  };
  if (mongoCol) {
    try {
      status.totalDocs = await mongoCol.countDocuments();
      const devResult = await mongoCol.aggregate([{ $group: { _id: '$device_id' } }]).toArray();
      status.devices = devResult.map(d => d._id);
    } catch (e) {
      status.mongoError = e.message;
    }
  }
  res.json(status);
});

// Add WebSocket bridge so browsers can receive MQTT messages via ws://<host>:<port>/ws
const http = require('http');
const WebSocket = require('ws');
const server = http.createServer(app);
const wss = new WebSocket.Server({ noServer: true, path: '/ws' });

// Upgrade HTTP connections to WebSocket for the WS bridge
server.on('upgrade', function upgrade(request, socket, head) {
  if (request.url === '/ws') {
    wss.handleUpgrade(request, socket, head, function done(ws) {
      wss.emit('connection', ws, request);
    });
  } else {
    socket.destroy();
  }
});

// NOTE: Removed duplicate message handler that was causing double-saves
// The main handler is defined above and also broadcasts to WS clients

const os = require('os');

// Serve index.html for root and SPA routing
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, '../public/index.html'));
});

// Start the HTTP server (which also serves the WS bridge) and bind to all interfaces
// Initialize MongoDB and then start server
initMongo().then(() => {
  server.listen(PORT, '0.0.0.0', () => {
    console.log(`Telemetry bridge listening on http://0.0.0.0:${PORT} (WS at /ws)`);

  // Print network interfaces to help connect from other devices
  const nets = os.networkInterfaces();
    Object.keys(nets).forEach((name) => {
      for (const net of nets[name]) {
        if (net.family === 'IPv4' && !net.internal) {
          console.log(`Interface ${name}: ${net.address}`);
        }
      }
    });
  });
}).catch(err => {
  console.error('Startup error', err);
});

// Simple ping on new WS connections
wss.on('connection', (ws) => {
  console.log('WebSocket client connected');
  ws.send(JSON.stringify({ hello: 'ws-bridge', ts: Date.now() }));
  ws.on('close', () => console.log('WebSocket client disconnected'));
});
