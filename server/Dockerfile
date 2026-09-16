# Fly.io build for the MQTT -> MongoDB bridge.
# Build context is this `server/` directory (see fly.toml).

FROM node:20-slim

# sqlite3 is a native module; install a toolchain in case no prebuilt
# binary matches this platform and it has to compile from source.
RUN apt-get update && apt-get install -y --no-install-recommends \
      python3 \
      make \
      g++ \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy manifests first so dependency layers cache across code changes.
COPY package.json package-lock.json ./
RUN npm ci --omit=dev

COPY . .

ENV NODE_ENV=production
ENV PORT=3000
EXPOSE 3000

CMD ["node", "index.js"]
