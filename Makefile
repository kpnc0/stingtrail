CC ?= cc
CFLAGS ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pedantic -Iinclude -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2

BUILD_DIR = build

# Core common sources (Zero-dependency libc only)
SRC_COMMON = src/json.c src/guard.c src/store.c src/state.c

# MCP wrapper sources (Zero-dependency libc only)
SRC_MCP_CORE = src/mcp.c $(SRC_COMMON)
SRC_MCP = src/mcp_main.c $(SRC_MCP_CORE)
SRC_MCP_TEST = tests/test_mcp.c $(SRC_MCP_CORE)
TARGET_MCP = $(BUILD_DIR)/stingtrail-mcp
TEST_MCP = $(BUILD_DIR)/test_mcp

# CLI binary & test (Zero external dependencies)
SRC_CLI = src/main.c $(SRC_MCP_CORE)
SRC_CLI_TEST = tests/test_stingtrail.c $(SRC_COMMON)
TARGET_CLI = $(BUILD_DIR)/stingtrail
TEST_CLI = $(BUILD_DIR)/test_stingtrail

# Read-only GUI binary & test (Raven retained-mode UI)
SRC_GUI = src/gui.c src/gui_main.c $(SRC_COMMON)
SRC_GUI_TEST = tests/test_gui.c src/gui.c $(SRC_COMMON)
TARGET_GUI = $(BUILD_DIR)/stingtrail-gui
TEST_GUI = $(BUILD_DIR)/test_gui
GUI_LIBS = -lraven -lX11 -lm -lpthread

.PHONY: all test test-cli test-mcp test-gui clean

all: $(TARGET_CLI) $(TARGET_MCP) $(TARGET_GUI)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build CLI
$(TARGET_CLI): $(SRC_CLI) include/stingtrail.h include/stingtrail_mcp.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_CLI) -o $(TARGET_CLI)

$(TEST_CLI): $(SRC_CLI_TEST) include/stingtrail.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_CLI_TEST) -o $(TEST_CLI)

# Build MCP Server
$(TARGET_MCP): $(SRC_MCP) include/stingtrail.h include/stingtrail_mcp.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_MCP) -o $(TARGET_MCP)

$(TEST_MCP): $(SRC_MCP_TEST) include/stingtrail.h include/stingtrail_mcp.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_MCP_TEST) -o $(TEST_MCP)

# Build Read-Only GUI
$(TARGET_GUI): $(SRC_GUI) include/stingtrail.h include/stingtrail_gui.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_GUI) -o $(TARGET_GUI) $(GUI_LIBS)

$(TEST_GUI): $(SRC_GUI_TEST) include/stingtrail.h include/stingtrail_gui.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_GUI_TEST) -o $(TEST_GUI) $(GUI_LIBS)

test-cli: $(TEST_CLI)
	@echo "--- Running Stingtrail CLI test suite ---"
	./$(TEST_CLI)

test-mcp: $(TEST_MCP)
	@echo "--- Running Stingtrail MCP test suite ---"
	./$(TEST_MCP)

test-gui: $(TEST_GUI)
	@echo "--- Running Stingtrail-GUI test suite ---"
	./$(TEST_GUI)

test: test-cli test-mcp test-gui
	@echo "=== All Stingtrail test suites passed successfully! ==="

clean:
	rm -rf $(BUILD_DIR) test_tmp_*.jsonl
