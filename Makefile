# Host-side checks. The device build is scripts/build.sh.
CFLAGS = -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -Isrc/include
ENGINE = src/engine/e1_engine.c

.PHONY: test preview clean
test: build/test_engine build/test_plugin build/test_lite
	./build/test_engine
	./build/test_plugin
	./build/test_lite

build/test_%: tests/test_%.c $(ENGINE) src/engine/e1_engine.h $(wildcard src/*/dsp/*.c)
	@mkdir -p build
	$(CC) $(CFLAGS) $< $(ENGINE) -lm -o $@

# Needs a Schwung checkout: make preview SCHWUNG=../schwung
preview:
	SCHWUNG=$(SCHWUNG) node tools/preview.mjs --png docs/screens

clean:
	rm -rf build dist
