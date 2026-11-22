# Host renderer for the gauge screens: no board, real LVGL, real UI sources.
#
#   make -C tools/sim
#   ./tools/sim/build/render /tmp/gauge-render
#   ./tools/sim/preview.py /tmp/gauge-render assets
#
# LVGL 9.3.0 comes from the PlatformIO libdeps checkout when one exists (the
# same library the firmware links); otherwise it is downloaded into
# tools/sim/build/lvgl. include/lv_conf.h is used verbatim - a second LVGL
# configuration for the host would drift from the panel's, and a render of the
# wrong configuration is worse than no render.

ROOT := $(abspath $(CURDIR)/../..)
BUILD := $(CURDIR)/build

# The pinned firmware LVGL if the project has been built, else a fresh copy.
LVGL := $(shell if [ -f "$(ROOT)/.pio/libdeps/esp32s3/lvgl/lvgl.h" ]; then \
	echo "$(ROOT)/.pio/libdeps/esp32s3/lvgl"; else echo "$(BUILD)/lvgl"; fi)

CC ?= cc
CXX ?= c++

WARN := -Wall -Wextra -Wno-unused-parameter
INCLUDES := -I$(LVGL) -I$(ROOT)/include -I$(ROOT)/src -I$(ROOT)/tools/sim/host
DEFS := -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE \
	-DLV_INV_BUF_SIZE=128 \
	-DSIM_SPLASH_RAW='"$(BUILD)/splash-466.rgb565"'
CFLAGS := -O1 -g $(WARN) $(INCLUDES) $(DEFS) -std=gnu11
# Host gcc diagnoses the UI sources' bounded snprintf() calls as if the int
# were unbounded; the Xtensa toolchain does not, and the values here are
# tenths clamped to the scale. Silenced so the renderer's own output is
# readable, not because the files are wrong for the firmware.
CXXFLAGS := -O1 -g $(WARN) -Wno-format-truncation $(INCLUDES) $(DEFS) \
	-std=gnu++17

# The UI translation units, exactly as the firmware compiles them. The rest of
# src/ is the radio, the panel and the IMU, which a render does not touch -
# tools/sim/sim_support.cpp stands in for settings, ohx_now and splashimg.
UI_SRCS := src/ui/theme.cpp src/ui/gauge_ui.cpp src/ui/oil_ui.cpp \
	src/ui/oil_press_ui.cpp src/ui/coolant_ui.cpp src/ui/drive_ui.cpp \
	src/ui/gmeter_ui.cpp src/ui/splash.cpp src/ui/font_num_96.c
SIM_SRCS := tools/sim/sim_support.cpp tools/sim/render.cpp

PROJECT_SRCS := $(addprefix $(ROOT)/,$(UI_SRCS) $(SIM_SRCS))
PROJECT_OBJS := $(patsubst $(ROOT)/%.cpp,$(BUILD)/obj/%.o, \
	$(patsubst $(ROOT)/%.c,$(BUILD)/obj/%.o,$(PROJECT_SRCS)))

LVGL_OBJS := $(patsubst $(LVGL)/src/%.c,$(BUILD)/obj/lvgl/%.o, \
	$(shell find "$(LVGL)/src" -name '*.c' 2>/dev/null))

.PHONY: all clean fetch-lvgl

all:
	@test -f "$(LVGL)/lvgl.h" || $(MAKE) --no-print-directory fetch-lvgl
	@$(MAKE) --no-print-directory $(BUILD)/render

fetch-lvgl:
	@echo "[sim] fetching LVGL 9.3.0 into tools/sim/build/lvgl"
	@mkdir -p $(BUILD)
	@curl -fsSL \
		https://github.com/lvgl/lvgl/archive/refs/tags/v9.3.0.tar.gz \
		-o $(BUILD)/lvgl.tar.gz
	@tar xzf $(BUILD)/lvgl.tar.gz -C $(BUILD)
	@mv $(BUILD)/lvgl-9.3.0 $(BUILD)/lvgl
	@rm -f $(BUILD)/lvgl.tar.gz

$(BUILD)/render: $(PROJECT_OBJS) $(LVGL_OBJS) | $(BUILD)/splash-466.rgb565
	@echo "[sim] link $@"
	@$(CXX) $(PROJECT_OBJS) $(LVGL_OBJS) -o $@ -lm

$(BUILD)/obj/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	@echo "[sim] $(<:$(ROOT)/%=%)"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/lvgl/%.o: $(LVGL)/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/splash-466.rgb565: $(ROOT)/assets/canvas-gauge-splash-466-base.jpg \
		$(ROOT)/tools/sim/splash_raw.py
	@mkdir -p $(BUILD)
	@python3 $(ROOT)/tools/sim/splash_raw.py $< $@

clean:
	@rm -rf $(BUILD)
