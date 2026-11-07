# The terminal on a PC, for vttest: see harness.c. Built under Linux or WSL, out of the tree: make OUT=$HOME/vt2350/h
OUT ?= $(HOME)/vt2350/h
R = ../..
V = $(R)/third_party/libvterm
CFLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I. -I$(R)/src -I$(V)/include -I$(V)/src -DVT_HARNESS -DVTERM_MAX_CHARS_PER_CELL=1 -Wall -Wno-unused-function
HEAP = -Dmalloc=h_malloc -Dfree=h_free -include heap.h
SRC_HEAP = $(R)/src/vt_term.c $(V)/src/encoding.c $(V)/src/keyboard.c $(V)/src/mouse.c $(V)/src/parser.c $(V)/src/pen.c $(V)/src/screen.c $(V)/src/state.c $(V)/src/unicode.c $(V)/src/vterm.c
SRC = harness.c $(R)/src/vt100.c $(R)/src/vt_fonts.c $(R)/src/xterm_palette.c

$(OUT)/harness: $(SRC) $(SRC_HEAP) heap.h
	mkdir -p $(OUT)
	for f in $(SRC_HEAP); do $(CC) $(CFLAGS) $(HEAP) -std=gnu11 -Wno-all -c $$f -o $(OUT)/h_$$(basename $$f .c).o || exit 1; done
	for f in $(SRC); do $(CC) $(CFLAGS) -std=gnu11 -c $$f -o $(OUT)/$$(basename $$f .c).o || exit 1; done
	$(CC) $(CFLAGS) $(OUT)/*.o -o $@ -lutil
