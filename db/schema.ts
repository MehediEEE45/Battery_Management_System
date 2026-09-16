import { pgTable, serial, text, doublePrecision, bigint, jsonb, timestamp, index } from "drizzle-orm/pg-core";

export const readings = pgTable("readings", {
  id: serial().primaryKey(),
  deviceId: text("device_id").notNull(),
  topic: text("topic").notNull(),
  voltage: doublePrecision("voltage"),
  current: doublePrecision("current"),
  power: doublePrecision("power"),
  temperature: doublePrecision("temperature"),
  socPercent: doublePrecision("soc_percent"),
  sohPercent: doublePrecision("soh_percent"),
  uptimeMs: doublePrecision("uptime_ms"),
  payload: jsonb("payload").notNull(),
  ts: bigint("ts", { mode: "number" }).notNull(),
  createdAt: timestamp("created_at").defaultNow(),
}, (table) => [
  index("readings_device_ts_idx").on(table.deviceId, table.ts),
]);
