CREATE TABLE "readings" (
	"id" serial PRIMARY KEY,
	"device_id" text NOT NULL,
	"topic" text NOT NULL,
	"voltage" double precision,
	"current" double precision,
	"power" double precision,
	"temperature" double precision,
	"soc_percent" double precision,
	"soh_percent" double precision,
	"uptime_ms" double precision,
	"payload" jsonb NOT NULL,
	"ts" bigint NOT NULL,
	"created_at" timestamp DEFAULT now()
);
--> statement-breakpoint
CREATE INDEX "readings_device_ts_idx" ON "readings" ("device_id","ts");