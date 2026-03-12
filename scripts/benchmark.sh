#!/usr/bin/env bash
# ============================================================
# benchmark.sh - 压测脚本（需要 wrk 或 ab）
# 用法：./scripts/benchmark.sh [host] [port]
# ============================================================

set -euo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-9006}"
URL="http://${HOST}:${PORT}/index.html"
DURATION="30s"
THREADS=4
CONNECTIONS=1000

log() { echo -e "\033[1;36m[BENCH]\033[0m $*"; }
sep() { echo "────────────────────────────────────────────────────────"; }

log "Target: ${URL}"
log "Duration: ${DURATION} | Threads: ${THREADS} | Connections: ${CONNECTIONS}"
sep

# ── wrk ──────────────────────────────────────────────────────────────────────
if command -v wrk &>/dev/null; then
  log "Running wrk..."
  wrk -t${THREADS} -c${CONNECTIONS} -d${DURATION} \
      --latency "${URL}"
  sep

# ── ab (Apache Bench) ─────────────────────────────────────────────────────────
elif command -v ab &>/dev/null; then
  log "Running ab..."
  ab -n 100000 -c ${CONNECTIONS} -k "${URL}"
  sep

# ── webbench ──────────────────────────────────────────────────────────────────
elif command -v webbench &>/dev/null; then
  log "Running webbench..."
  webbench -c ${CONNECTIONS} -t 30 "${URL}"
  sep

else
  echo "No benchmark tool found. Install wrk, ab, or webbench."
  echo "  Ubuntu: sudo apt install apache2-utils"
  echo "  wrk:    https://github.com/wg/wrk"
  exit 1
fi

log "Done."
