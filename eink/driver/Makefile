# epaper – viser et bilde på Waveshare 10,3" IT8951.
#
#   make                bygg med lgpio (standard)
#   make LIB=BCM        bygg med bcm2835 i stedet (krever sudo ved kjøring)
#   make test           kjør pakketestene (trenger ingen skjerm)
#   make check          syntakssjekk alt uten å lenke (virker på Mac)

LIB             ?= LGPIO
VCOM_DEFAULT_MV ?= 1140          # -1,14 V. Overstyres av EPAPER_VCOM.
# SPI-frekvens for LGPIO. 7 812 500 Hz er det samme som BCM-veien kjører på
# en Pi 3 (250 MHz / 32), altså den farten panelet er verifisert på. Waveshare
# hardkodet 12 500 000, som ikke nødvendigvis virker på alle paneler.
SPI_HZ          ?= 7812500

SRC_DIR = src
VEN_DIR = vendor/it8951
BIN_DIR = build
TARGET  = epaper
CC     ?= gcc

WARN   = -Wall -Wextra -Wno-unused-parameter
CFLAGS += -std=gnu11 $(WARN) -O2 -g -fno-strict-aliasing \
          -I$(SRC_DIR) -I$(VEN_DIR) \
          -DEPAPER_VCOM_DEFAULT_MV=$(VCOM_DEFAULT_MV) \
          -DEPAPER_SPI_HZ=$(SPI_HZ)
LDLIBS  = -lm -lrt -lpthread

# -fno-strict-aliasing er ikke valgfritt: driveren caster vår UBYTE*-buffer
# til UWORD* i HostAreaPackedPixelWrite_4bp, og det har -O2 ellers lov til å
# optimalisere i stykker.

ifeq ($(LIB),LGPIO)
  LDLIBS += -llgpio
else ifeq ($(LIB),BCM)
  LDLIBS += -lbcm2835
else
  $(error LIB maa vaere LGPIO eller BCM (fikk "$(LIB)"))
endif

OUR_SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/bmp.c $(SRC_DIR)/pack.c
VEN_SRCS = $(VEN_DIR)/EPD_IT8951.c $(VEN_DIR)/DEV_Config.c
OBJS     = $(patsubst %.c,$(BIN_DIR)/%.o,$(OUR_SRCS) $(VEN_SRCS))

.PHONY: all clean test check
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# DEV_Config.c er den eneste fila som rører lgpio/bcm2835, så -D$(LIB) trengs
# bare her. Det holder resten av koden fri for GPIO-headere, og er grunnen til
# at `make test` kan bygge pakkingen på en helt vanlig maskin.
$(BIN_DIR)/$(VEN_DIR)/DEV_Config.o: $(VEN_DIR)/DEV_Config.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -D$(LIB) -MMD -MP -c $< -o $@

# Vendret kode har noen advarsler vi ikke vil rydde i; de er ikke våre.
$(BIN_DIR)/$(VEN_DIR)/%.o: $(VEN_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-but-set-variable -Wno-unused-function -MMD -MP -c $< -o $@

$(BIN_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

test:
	@mkdir -p $(BIN_DIR)
	$(CC) -std=gnu11 $(WARN) -O2 -I$(SRC_DIR) \
	      tests/test_pack.c $(SRC_DIR)/pack.c -o $(BIN_DIR)/test_pack
	@$(BIN_DIR)/test_pack

# Syntakssjekk uten lenking, mot en stubbet lgpio.h. Lar oss fange skrivefeil
# på en maskin uten liblgpio – typisk utviklingsmaskina.
check:
	$(CC) -fsyntax-only -std=gnu11 $(WARN) -D$(LIB) \
	      -I$(SRC_DIR) -I$(VEN_DIR) -Itests/stubs \
	      $(OUR_SRCS) $(VEN_SRCS) \
	      -Wno-unused-but-set-variable -Wno-unused-function
	@echo "check: ok"

clean:
	rm -rf $(BIN_DIR) $(TARGET)

-include $(shell find $(BIN_DIR) -name '*.d' 2>/dev/null)
