# ============================================================
# Makefile - CMake 不可用时的备用构建方案
# 用法：
#   make          # Release 构建
#   make debug    # Debug 构建（含 ASan/UBSan）
#   make test     # 编译并运行单元测试
#   make clean    # 清理
# ============================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wshadow -Wno-unused-parameter
INCLUDES := -Iinclude
LDFLAGS  := -lpthread -lstdc++fs

# 检查 MySQL 是否可用
MYSQL_INC := $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIB := $(shell mysql_config --libs   2>/dev/null)
ifneq ($(MYSQL_INC),)
    CXXFLAGS += -DHAVE_MYSQL $(MYSQL_INC)
    LDFLAGS  += $(MYSQL_LIB)
    MYSQL_SRC := src/pool/SqlConnPool.cpp
endif

# 源文件
SRCS := \
    src/buffer/Buffer.cpp     \
    src/log/Log.cpp           \
    src/timer/TimerHeap.cpp   \
    src/http/HttpRequest.cpp  \
    src/http/HttpResponse.cpp \
    src/http/HttpConn.cpp     \
    src/server/Server.cpp     \
    src/main.cpp              \
    $(MYSQL_SRC)

OBJS    := $(SRCS:%.cpp=build/%.o)
TARGET  := build/webserver

# ── Release ──────────────────────────────────────────────────────────────────
.PHONY: all
all: CXXFLAGS += -O2 -DNDEBUG
all: $(TARGET)
	@echo "✓ Build complete: $(TARGET)"

# ── Debug ────────────────────────────────────────────────────────────────────
.PHONY: debug
debug: CXXFLAGS += -g3 -fsanitize=address,undefined
debug: LDFLAGS  += -fsanitize=address,undefined
debug: $(TARGET)
	@echo "✓ Debug build complete: $(TARGET)"

# ── 链接 ─────────────────────────────────────────────────────────────────────
$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# ── 编译 ─────────────────────────────────────────────────────────────────────
build/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ── 测试 ─────────────────────────────────────────────────────────────────────
TEST_SRCS_BASE := src/buffer/Buffer.cpp src/log/Log.cpp \
                  src/http/HttpRequest.cpp src/pool/SqlConnPool.cpp

.PHONY: test
test: build/test_buffer build/test_timer build/test_httprequest
	@echo "══════════════════════════════════"
	@echo "Running test_buffer..."
	@./build/test_buffer
	@echo "Running test_timer..."
	@./build/test_timer
	@echo "Running test_httprequest..."
	@./build/test_httprequest
	@echo "══════════════════════════════════"
	@echo "All tests passed!"

build/test_buffer: tests/test_buffer.cpp src/buffer/Buffer.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -g $(INCLUDES) $^ -o $@ $(LDFLAGS)

build/test_timer: tests/test_timer.cpp src/timer/TimerHeap.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -g $(INCLUDES) $^ -o $@ $(LDFLAGS)

build/test_httprequest: tests/test_httprequest.cpp $(TEST_SRCS_BASE)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -g $(INCLUDES) $^ -o $@ $(LDFLAGS) $(MYSQL_LIB)

# ── 清理 ─────────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf build/
	@echo "✓ Cleaned."
