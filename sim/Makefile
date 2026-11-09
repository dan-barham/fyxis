# Builds the Mac simulator. Run ./run.sh rather than make directly; it fetches LVGL first.
# LVGL: the copy the firmware build uses (same version), unless ./lvgl exists
LVGL    ?= $(if $(wildcard lvgl/src),lvgl,../firmware/deps/lvgl)
SDL_CF  := $(shell sdl2-config --cflags)
SDL_LD  := $(shell sdl2-config --libs)
CFLAGS  := -O2 -w -I. -I$(LVGL) -DLV_CONF_INCLUDE_SIMPLE $(SDL_CF)
LDLIBS  := $(SDL_LD) -lm -lpthread

LV_SRC  := $(shell find $(LVGL)/src -name '*.c')
LV_OBJ  := $(patsubst $(LVGL)/%.c,build/lvgl/%.o,$(LV_SRC))
FONTS   := font_label_14 font_label_20 font_tc_26 font_icons_22 font_icons_28 font_icons_48 font_mid_36 font_value_64 font_big_132
APP_OBJ := build/app/main_sim.o build/app/camera_sim.o build/app/camproto.o build/app/bigsix.o build/app/recbutton.o build/app/settings.o build/app/update.o \
           $(FONTS:%=build/app/%.o)

bigsix_sim: $(APP_OBJ) $(LV_OBJ)
	$(CC) -o $@ $^ $(LDLIBS)

build/lvgl/%.o: $(LVGL)/%.c lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

build/app/%.o: %.c lv_conf.h ../main/camera.h ../main/camproto.h ../main/recbutton.h ../main/settings.h ../main/update.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/app/%.o: ../main/%.c lv_conf.h ../main/camera.h ../main/camproto.h ../main/recbutton.h ../main/settings.h ../main/update.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build bigsix_sim

.PHONY: clean
