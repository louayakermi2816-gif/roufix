# Compilation des puces Wokwi en WebAssembly.
# Lance par build-chips.bat, via l'image Docker fournie par Wokwi.
CHIPS = pc817 alim24

CFLAGS = --target=wasm32-unknown-wasi --sysroot /opt/wasi-libc -nostartfiles -I. -Werror
LDFLAGS = -Wl,--import-memory -Wl,--export-table -Wl,--no-entry

all: $(foreach c,$(CHIPS),$(c)/$(c).chip.wasm)

%.chip.wasm: %.chip.c wokwi-api.h
	clang $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f */*.chip.wasm
