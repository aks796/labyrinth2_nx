#---------------------------------------------------------------------------------
# Labyrinth 2 -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the game's own APK (the user's copy,
# any name) is read at run time; its library and data are unpacked from it
# on the first launch (source/lab_main.c, source/lab_files.c); the menus'
# pictures and the sounds are read from the APK itself.
#
# The build is the android32 runtime's (runtime/runtime.mk: devkitARM +
# libnx32 + mesa32 from portlibs32/); ./build.sh runs it in the toolchain
# container. Output: labyrinth2_nx.nsp -- an ExeFS NSP (main + main.npdm,
# 32-bit), which the launcher NRO carries (launcher/). A 32-bit program
# cannot be an NRO: hbloader is 64-bit.
#---------------------------------------------------------------------------------
TARGET               := labyrinth2_nx
PORT_NPDM_PROGRAM_ID := 0x0100000000001010
include runtime/runtime.mk

# stb_vorbis / stb_image (public domain) are compiled into lab_audio.c and
# lab_draw.c; they warn about things that are theirs, not ours.
$(BUILD)/lab_audio.o $(BUILD)/lab_draw.o: CFLAGS += -Wno-sign-compare -Wno-unused-function \
  -Wno-unused-value -Wno-misleading-indentation -Wno-shadow -Wno-implicit-fallthrough \
  -Wno-type-limits -Wno-unused-but-set-variable -Wno-maybe-uninitialized -Wno-array-bounds
$(BUILD)/lab_audio.o: $(SOURCES)/stb_vorbis.inc

.PHONY: check
check:
	@echo "run on the host: python3 runtime/tools/gen_imports.py --check"
