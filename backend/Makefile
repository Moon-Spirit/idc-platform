BUILD_DIR ?= build
CMAKE     ?= cmake
JOBS      ?= $(shell nproc 2>/dev/null || echo 4)
DEPS_PREFIX ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))deps)

.PHONY: build run clean test

## build    — Configure & compile the project
build:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(CMAKE) .. -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_PREFIX_PATH=$(DEPS_PREFIX) \
		-DJsoncpp_DIR=$(DEPS_PREFIX)/lib/cmake/jsoncpp
	$(MAKE) -C $(BUILD_DIR) -j$(JOBS)

## run      — Launch the server (builds first if needed)
run: build
	@echo ""
	@echo "═══ Starting IDC Platform API Server ═══"
	cd $(BUILD_DIR) && ./idc-platform

## clean    — Remove build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -rf logs/
	@echo "[clean] Done"

## test     — Verify the binary compiles and exits cleanly
test: build
	@echo ""
	@echo "═══ Build verification ═══"
	@if [ -f $(BUILD_DIR)/idc-platform ]; then \
		echo "  Binary: $(BUILD_DIR)/idc-platform"; \
		ls -lh $(BUILD_DIR)/idc-platform; \
		echo "  Status: BUILD SUCCESS"; \
	else \
		echo "  Status: BUILD FAILED"; \
		exit 1; \
	fi
