#!/usr/bin/env bash
# ============================================================
# build.sh - 构建 WebServer
# 用法：
#   ./scripts/build.sh           # Release 构建
#   ./scripts/build.sh debug     # Debug 构建（含 ASan）
#   ./scripts/build.sh clean     # 清理构建目录
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_TYPE="${1:-release}"
BUILD_DIR="${PROJECT_DIR}/build"

log() { echo -e "\033[1;32m[BUILD]\033[0m $*"; }
err() { echo -e "\033[1;31m[ERROR]\033[0m $*" >&2; exit 1; }

case "${BUILD_TYPE,,}" in
  clean)
    log "Cleaning build directory..."
    rm -rf "${BUILD_DIR}"
    log "Done."
    exit 0
    ;;
  debug)
    CMAKE_BUILD_TYPE="Debug"
    ;;
  release|*)
    CMAKE_BUILD_TYPE="Release"
    ;;
esac

log "Build type: ${CMAKE_BUILD_TYPE}"
log "Project   : ${PROJECT_DIR}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${PROJECT_DIR}" \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  2>&1 | tail -20

CORES=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
cmake --build . --parallel "${CORES}"

log "Build succeeded → ${BUILD_DIR}/webserver"
log "Run: cd ${PROJECT_DIR} && ./build/webserver [conf/server.conf]"
