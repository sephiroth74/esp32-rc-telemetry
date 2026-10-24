# RC Telemetry — build / test / format entry points (PLAN.md).
#
# The pio CLI is not on PATH, so it is addressed explicitly. Override with
# `make PIO=/somewhere/pio` if your install lives elsewhere.
PIO ?= $(HOME)/.platformio/penv/bin/pio
FW := firmware

.PHONY: help build upload sim monitor test check fmt clean

help: ## Show this help
	@grep -hE '^[a-z-]+:.*##' $(MAKEFILE_LIST) | sed -e 's/:.*##/\t/' | expand -t 12

build: ## Compile the firmware
	$(PIO) run -d $(FW)

upload: ## Flash the firmware over USB
	$(PIO) run -d $(FW) -t upload

sim: ## Flash the bench simulation, driven from the console (include/bench_sim.h)
	$(PIO) run -d $(FW) -e sim -t upload

monitor: ## Open the serial monitor
	$(PIO) device monitor -d $(FW)

test: ## Run the host unit tests for lib/{ubx,rcl,geo,ui}
	$(PIO) test -d $(FW) -e native_test

check: ## Static analysis of the firmware sources
	$(PIO) check -d $(FW) --skip-packages

fmt: ## Format the firmware sources in place
	@find $(FW)/src $(FW)/include $(FW)/lib $(FW)/test \
		\( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 clang-format -i

clean: ## Remove build output
	$(PIO) run -d $(FW) -t fullclean
