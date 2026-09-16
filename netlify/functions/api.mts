import type { Config, Context } from "@netlify/functions";
import { and, desc, asc, eq, gte, lte, sql } from "drizzle-orm";
import mqtt from "mqtt";
import { db } from "../../db/index.js";
import { readings } from "../../db/schema.js";

const DEFAULT_BROKER = "wss://0d34f5789e1e4a669367abfe5bd45b15.s1.eu.hivemq.cloud:8884/mqtt";
const DEFAULT_MQTT_USER = "battery";
const DEFAULT_MQTT_PASS = "Batterybms80";
const DEFAULT_COMMAND_TOPIC = "battery/recieve";

function json(data: unknown, status = 200) {
  return Response.json(data, { status });
}

function numberOf(payload: Record<string, unknown> | null | undefined, ...keys: string[]) {
  if (!payload) return null;
  for (const key of keys) {
    const val = payload[key];
    if (val != null && !Number.isNaN(Number(val))) return Number(val);
  }
  return null;
}

function calcStats(arr: number[]) {
  if (arr.length === 0) return null;
  const sorted = [...arr].sort((a, b) => a - b);
  const avg = sorted.reduce((a, b) => a + b, 0) / sorted.length;
  const min = sorted[0];
  const max = sorted[sorted.length - 1];
  const mid = Math.floor(sorted.length / 2);
  const median = sorted.length % 2 === 0 ? (sorted[mid - 1] + sorted[mid]) / 2 : sorted[mid];
  return {
    avg: parseFloat(avg.toFixed(2)),
    min: parseFloat(min.toFixed(2)),
    max: parseFloat(max.toFixed(2)),
    median: parseFloat(median.toFixed(2)),
    count: sorted.length,
  };
}

type Row = typeof readings.$inferSelect;

function rowToDoc(row: Row) {
  return {
    device_id: row.deviceId,
    topic: row.topic,
    ts: row.ts,
    payload: row.payload,
  };
}

async function handleIngest(req: Request) {
  const body = await req.json().catch(() => null) as { device_id?: string; topic?: string; payload?: Record<string, unknown> } | null;
  if (!body || !body.device_id || !body.topic || !body.payload) {
    return json({ error: "Missing device_id, topic or payload" }, 400);
  }
  const payload = body.payload;
  const [row] = await db.insert(readings).values({
    deviceId: String(payload.device_id || body.device_id),
    topic: body.topic,
    voltage: numberOf(payload, "bus_V", "voltage"),
    current: numberOf(payload, "current_A", "current"),
    power: numberOf(payload, "power_W", "power"),
    temperature: numberOf(payload, "temperature"),
    socPercent: numberOf(payload, "soc_percent"),
    sohPercent: numberOf(payload, "soh_percent"),
    uptimeMs: numberOf(payload, "uptime_ms"),
    payload,
    ts: Date.now(),
  }).returning();
  return json({ success: true, id: row.id });
}

async function handleRecentReadings(deviceId: string, url: URL) {
  const limit = parseInt(url.searchParams.get("limit") || "100", 10);
  const rows = await db.select().from(readings)
    .where(eq(readings.deviceId, deviceId))
    .orderBy(desc(readings.ts))
    .limit(limit);
  return json(rows.reverse().map(rowToDoc));
}

async function handleStats(deviceId: string, url: URL) {
  const hours = parseInt(url.searchParams.get("hours") || "24", 10);
  const cutoff = Date.now() - hours * 60 * 60 * 1000;
  const rows = await db.select().from(readings)
    .where(and(eq(readings.deviceId, deviceId), gte(readings.ts, cutoff)));

  if (rows.length === 0) {
    return json({ device_id: deviceId, count: 0, voltage: null, current: null, power: null, soc: null });
  }

  const docs = rows.map(rowToDoc);
  const getNumbers = (keys: string[]) => docs.map(d => numberOf(d.payload as Record<string, unknown>, ...keys)).filter((v): v is number => v !== null);

  return json({
    device_id: deviceId,
    count: docs.length,
    hours,
    voltage: calcStats(getNumbers(["bus_V", "voltage"])),
    current: calcStats(getNumbers(["current_A", "current"])),
    power: calcStats(getNumbers(["power_W", "power"])),
    temperature: calcStats(getNumbers(["temperature"])),
    soc: calcStats(getNumbers(["soc_percent"])),
    soh: calcStats(getNumbers(["soh_percent"])),
    ts_range: { from: docs[0].ts, to: docs[docs.length - 1].ts },
  });
}

async function get30DayRows(deviceId: string, url: URL) {
  const days = parseInt(url.searchParams.get("days") || "30", 10);
  const cutoff = Date.now() - days * 24 * 60 * 60 * 1000;
  const rows = await db.select().from(readings)
    .where(and(eq(readings.deviceId, deviceId), gte(readings.ts, cutoff)))
    .orderBy(asc(readings.ts));
  return { days, rows };
}

async function handleReadings30Days(deviceId: string, url: URL) {
  const { days, rows } = await get30DayRows(deviceId, url);
  const docs = rows.map(rowToDoc);
  return json({ device_id: deviceId, days, count: docs.length, data: docs });
}

async function handleStats30Days(deviceId: string, url: URL) {
  const { days, rows } = await get30DayRows(deviceId, url);
  if (rows.length === 0) {
    return json({ device_id: deviceId, days, count: 0, stats: null });
  }
  const docs = rows.map(rowToDoc);
  const getNumbers = (keys: string[]) => docs.map(d => numberOf(d.payload as Record<string, unknown>, ...keys)).filter((v): v is number => v !== null);

  const voltages = getNumbers(["bus_V", "voltage"]);
  const currents = getNumbers(["current_A", "current"]);
  const powers = getNumbers(["power_W", "power"]);
  const temperatures = getNumbers(["temperature"]);
  const socs = getNumbers(["soc_percent"]);
  const sohs = getNumbers(["soh_percent"]);

  // Assumes ~5 second sample interval, matching the original bridge's estimate
  const energyKwh = (powers.reduce((a, b) => a + b, 0) * 5 / 3600 / 1000).toFixed(2);

  return json({
    device_id: deviceId,
    days,
    total_records: docs.length,
    timestamp_range: { from: new Date(docs[0].ts as number), to: new Date(docs[docs.length - 1].ts as number) },
    stats: {
      voltage: calcStats(voltages),
      current: calcStats(currents),
      power: calcStats(powers),
      temperature: calcStats(temperatures),
      energy_kwh: energyKwh,
      soc: calcStats(socs),
      soh: calcStats(sohs),
    },
  });
}

async function handleTrends30Days(deviceId: string, url: URL) {
  const { days, rows } = await get30DayRows(deviceId, url);
  if (rows.length === 0) return json({ device_id: deviceId, trends: [] });
  const docs = rows.map(rowToDoc);

  const byDay = new Map<string, typeof docs>();
  for (const doc of docs) {
    const date = new Date(doc.ts as number).toISOString().split("T")[0];
    if (!byDay.has(date)) byDay.set(date, []);
    byDay.get(date)!.push(doc);
  }

  const avg = (arr: number[]) => (arr.length ? (arr.reduce((a, b) => a + b, 0) / arr.length).toFixed(2) : null);

  const trends = Array.from(byDay.entries()).map(([date, dayDocs]) => {
    const p = (d: typeof dayDocs[number], ...keys: string[]) => numberOf(d.payload as Record<string, unknown>, ...keys);
    const voltages = dayDocs.map(d => p(d, "bus_V", "voltage")).filter((v): v is number => v !== null);
    const currents = dayDocs.map(d => p(d, "current_A", "current")).filter((v): v is number => v !== null);
    const powers = dayDocs.map(d => p(d, "power_W", "power")).filter((v): v is number => v !== null);
    const temperatures = dayDocs.map(d => p(d, "temperature")).filter((v): v is number => v !== null);
    const socs = dayDocs.map(d => p(d, "soc_percent")).filter((v): v is number => v !== null);
    const energyKwh = powers.length ? (powers.reduce((a, b) => a + b, 0) * 5 / 3600 / 1000).toFixed(2) : "0";

    return {
      date,
      count: dayDocs.length,
      voltage_avg: avg(voltages),
      current_avg: avg(currents),
      power_avg: avg(powers),
      temperature_avg: avg(temperatures),
      energy_kwh: energyKwh,
      soc_avg: avg(socs),
    };
  });

  return json({ device_id: deviceId, days, total_days: trends.length, trends });
}

async function handleExport(format: string, deviceId: string, url: URL) {
  const { days, rows } = await get30DayRows(deviceId, url);
  const docs = rows.map(rowToDoc);

  if (format === "csv") {
    if (docs.length === 0) return json({ error: "No data found" }, 404);
    let csv = "Timestamp,Date,Voltage (V),Current (A),Power (W),Temperature (C),SoC (%),SoH (%),Uptime (ms)\n";
    for (const doc of docs) {
      const p = (doc.payload || {}) as Record<string, unknown>;
      const date = new Date(doc.ts as number);
      csv += `${doc.ts},"${date.toISOString()}",${numberOf(p, "bus_V", "voltage") ?? ""},${numberOf(p, "current_A", "current") ?? ""},${numberOf(p, "power_W", "power") ?? ""},${numberOf(p, "temperature") ?? ""},${numberOf(p, "soc_percent") ?? ""},${numberOf(p, "soh_percent") ?? ""},${numberOf(p, "uptime_ms") ?? ""}\n`;
    }
    return new Response(csv, {
      headers: {
        "Content-Type": "text/csv",
        "Content-Disposition": `attachment; filename="battery_data_${deviceId}_${days}days.csv"`,
      },
    });
  }

  if (format === "json") {
    const data = docs.map(doc => {
      const p = (doc.payload || {}) as Record<string, unknown>;
      return {
        timestamp: doc.ts,
        date: new Date(doc.ts as number).toISOString(),
        voltage: numberOf(p, "bus_V", "voltage"),
        current: numberOf(p, "current_A", "current"),
        power: numberOf(p, "power_W", "power"),
        temperature: numberOf(p, "temperature"),
        soc: numberOf(p, "soc_percent"),
        soh: numberOf(p, "soh_percent"),
        uptime_ms: numberOf(p, "uptime_ms"),
      };
    });
    return new Response(JSON.stringify({
      device_id: deviceId,
      days,
      total_records: docs.length,
      exported_at: new Date().toISOString(),
      data,
    }), {
      headers: {
        "Content-Type": "application/json",
        "Content-Disposition": `attachment; filename="battery_data_${deviceId}_${days}days.json"`,
      },
    });
  }

  return json({ error: "Unknown export format" }, 400);
}

async function handleSync(deviceId: string, req: Request) {
  const body = await req.json().catch(() => null) as { readings?: Array<{ timestamp?: string | number; payload?: Record<string, unknown> }> } | null;
  const list = body?.readings;
  if (!Array.isArray(list) || list.length === 0) return json({ error: "readings must be a non-empty array" }, 400);

  const values = list.map(r => {
    const p = (r.payload || r || {}) as Record<string, unknown>;
    const ts = r.timestamp ? new Date(r.timestamp).getTime() : Date.now();
    return {
      deviceId,
      topic: "sync/browser",
      voltage: numberOf(p, "bus_V", "voltage"),
      current: numberOf(p, "current_A", "current"),
      power: numberOf(p, "power_W", "power"),
      temperature: numberOf(p, "temperature"),
      socPercent: numberOf(p, "soc_percent"),
      sohPercent: numberOf(p, "soh_percent"),
      uptimeMs: numberOf(p, "uptime_ms"),
      payload: p,
      ts,
    };
  });

  const inserted = await db.insert(readings).values(values).returning({ id: readings.id });
  return json({ success: true, insertedCount: inserted.length });
}

async function handleDeleteAll(deviceId: string) {
  const deleted = await db.delete(readings).where(eq(readings.deviceId, deviceId)).returning({ id: readings.id });
  return json({ success: true, deletedCount: deleted.length });
}

async function handleDeleteRange(deviceId: string, url: URL) {
  const from = url.searchParams.get("from") ? new Date(url.searchParams.get("from")!).getTime() : 0;
  const to = url.searchParams.get("to") ? new Date(url.searchParams.get("to")!).getTime() : Date.now();
  const deleted = await db.delete(readings)
    .where(and(eq(readings.deviceId, deviceId), gte(readings.ts, from), lte(readings.ts, to)))
    .returning({ id: readings.id });
  return json({ success: true, deletedCount: deleted.length });
}

async function handleCommand(deviceId: string, req: Request) {
  const body = await req.json().catch(() => null) as { command?: string; value?: unknown } | null;
  if (!body?.command) return json({ error: "Missing command field" }, 400);

  const broker = Netlify.env.get("MQTT_BROKER_URL") || DEFAULT_BROKER;
  const username = Netlify.env.get("MQTT_USERNAME") || DEFAULT_MQTT_USER;
  const password = Netlify.env.get("MQTT_PASSWORD") || DEFAULT_MQTT_PASS;
  const topic = Netlify.env.get("MQTT_COMMAND_TOPIC") || DEFAULT_COMMAND_TOPIC;
  const payload = JSON.stringify({ command: body.command, value: body.value ?? null, ts: Date.now(), from: "web-dashboard", device_id: deviceId });

  return new Promise<Response>((resolve) => {
    const client = mqtt.connect(broker, { username, password, connectTimeout: 8000, reconnectPeriod: 0 });
    const finish = (res: Response) => { client.end(true); resolve(res); };
    const timer = setTimeout(() => finish(json({ error: "MQTT publish timed out" }, 504)), 10000);

    client.on("connect", () => {
      client.publish(topic, payload, { qos: 1 }, (err) => {
        clearTimeout(timer);
        if (err) return finish(json({ error: "Publish failed: " + err.message }, 500));
        finish(json({ success: true, topic, payload: JSON.parse(payload) }));
      });
    });
    client.on("error", (err) => {
      clearTimeout(timer);
      finish(json({ error: "MQTT connect failed: " + err.message }, 502));
    });
  });
}

async function handleStatus() {
  const [{ count }] = await db.select({ count: sql<number>`count(*)::int` }).from(readings);
  const deviceRows = await db.select({ deviceId: readings.deviceId }).from(readings).groupBy(readings.deviceId);
  const [latest] = await db.select({ ts: readings.ts }).from(readings).orderBy(desc(readings.ts)).limit(1);
  const liveWithinMs = 2 * 60 * 1000;
  const live = !!latest && Date.now() - Number(latest.ts) < liveWithinMs;

  return json({
    mqtt: live,
    mongo: true,
    totalDocs: count,
    devices: deviceRows.map(d => d.deviceId),
    ts: Date.now(),
  });
}

export default async (req: Request, _context: Context) => {
  const url = new URL(req.url);
  const parts = url.pathname.split("/").filter(Boolean); // e.g. ["api", "mongo", "readings", "esp32"]

  try {
    if (req.method === "POST" && parts[1] === "readings" && parts.length === 2) {
      return await handleIngest(req);
    }

    if (parts[1] === "status" && parts.length === 2 && req.method === "GET") {
      return await handleStatus();
    }

    if (parts[1] === "command" && parts.length === 3 && req.method === "POST") {
      return await handleCommand(parts[2], req);
    }

    if (parts[1] === "mongo") {
      const section = parts[2];

      if (section === "readings" && parts[3] === "30days" && parts.length === 5 && req.method === "GET") {
        return await handleReadings30Days(parts[4], url);
      }

      if (section === "stats" && parts[3] === "30days" && parts.length === 5 && req.method === "GET") {
        return await handleStats30Days(parts[4], url);
      }

      if (section === "trends" && parts[3] === "30days" && parts.length === 5 && req.method === "GET") {
        return await handleTrends30Days(parts[4], url);
      }

      if (section === "export" && parts.length === 5 && req.method === "GET") {
        return await handleExport(parts[3], parts[4], url);
      }

      if (section === "sync" && parts.length === 4 && req.method === "POST") {
        return await handleSync(parts[3], req);
      }

      if (section === "readings" && parts.length === 5 && parts[4] === "range" && req.method === "DELETE") {
        return await handleDeleteRange(parts[3], url);
      }

      if (section === "readings" && parts.length === 4 && req.method === "DELETE") {
        return await handleDeleteAll(parts[3]);
      }

      if (section === "readings" && parts.length === 4 && req.method === "GET") {
        return await handleRecentReadings(parts[3], url);
      }

      if (section === "stats" && parts.length === 4 && req.method === "GET") {
        return await handleStats(parts[3], url);
      }
    }

    return json({ error: "Not found" }, 404);
  } catch (e) {
    return json({ error: (e as Error).message }, 500);
  }
};

export const config: Config = {
  path: "/api/*",
};
