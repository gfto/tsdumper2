CC = $(CROSS)$(TARGET)cc
STRIP = $(CROSS)$(TARGET)strip
MKDEP = $(CC) -MP -MM -o $*.d $<
RM = rm -f
MV = mv -f

BUILD_ID = $(shell date +%Y%m%d_%H%M%S)
VERSION = $(shell cat RELEASE)
GIT_VER = $(shell git describe --tags --dirty --always 2>/dev/null)
ifeq "$(GIT_VER)" ""
GIT_VER = "release"
endif

ifndef V
Q = @
endif

CFLAGS ?= -O2 -ggdb \
 -W -Wall -Wextra \
 -Wshadow -Wformat-security -Wstrict-prototypes

DEFS = -DBUILD_ID=\"$(BUILD_ID)\" \
 -DVERSION=\"$(VERSION)\" -DGIT_VER=\"$(GIT_VER)\"
DEFS += -D_FILE_OFFSET_BITS=64

PREFIX ?= /usr/local

INSTALL_PRG = tsdumper2
INSTALL_PRG_DIR = $(subst //,/,$(DESTDIR)/$(PREFIX)/bin)

INSTALL_DOC = tsdumper2.1
INSTALL_DOC_DIR = $(subst //,/,$(DESTDIR)/$(PREFIX)/share/man/man1)

FUNCS_DIR = libfuncs
FUNCS_LIB = $(FUNCS_DIR)/libfuncs.a

tsdumper_SRC = \
 udp.c \
 util.c \
 process.c \
 tsdumper2.c
tsdumper_LIBS = -lpthread

tsdumper_OBJS = $(FUNCS_LIB) $(tsdumper_SRC:.c=.o)

CLEAN_OBJS = tsdumper2 $(tsdumper_SRC:.c=.o) $(tsdumper_SRC:.c=.d)

PROGS = tsdumper2

.PHONY: help distclean clean install uninstall

all: $(PROGS)

$(FUNCS_LIB): $(FUNCS_DIR)/libfuncs.h
	$(Q)echo "  MAKE	$(FUNCS_LIB)"
	$(Q)$(MAKE) -s -C $(FUNCS_DIR)

tsdumper2: $(tsdumper_OBJS)
	$(Q)echo "  LINK	tsdumper2"
	$(Q)$(CC) $(CFLAGS) $(DEFS) $(tsdumper_OBJS) $(tsdumper_LIBS) -o tsdumper2

%.o: %.c Makefile RELEASE
	@$(MKDEP)
	$(Q)echo "  CC	tsdumper2	$<"
	$(Q)$(CC) $(CFLAGS) $(DEFS) -c $<

-include $(tsdumper_SRC:.c=.d)

strip:
	$(Q)echo "  STRIP	$(PROGS)"
	$(Q)$(STRIP) $(PROGS)

clean:
	$(Q)echo "  RM	$(CLEAN_OBJS)"
	$(Q)$(RM) $(CLEAN_OBJS)

distclean: clean
	$(Q)$(MAKE) -s -C $(FUNCS_DIR) clean

install: all
	@install -d "$(INSTALL_PRG_DIR)"
	@echo "INSTALL $(INSTALL_PRG) -> $(INSTALL_PRG_DIR)"
	$(Q)-install $(INSTALL_PRG) "$(INSTALL_PRG_DIR)"
	@echo "INSTALL $(INSTALL_DOC) -> $(INSTALL_DOC_DIR)"
	$(Q)-install --mode 0644 $(INSTALL_DOC) "$(INSTALL_DOC_DIR)"

uninstall:
	@-for FILE in $(INSTALL_PRG); do \
		echo "RM       $(INSTALL_PRG_DIR)/$$FILE"; \
		rm "$(INSTALL_PRG_DIR)/$$FILE"; \
	done
	@-for FILE in $(INSTALL_DOC); do \
		echo "RM       $(INSTALL_DOC_DIR)/$$FILE"; \
		rm "$(INSTALL_DOC_DIR)/$$FILE"; \
	done

help:
	$(Q)echo -e "\
tsdumper2 $(VERSION) ($(GIT_VER)) build\n\n\
Build targets:\n\
  tsdumper2|all   - Build tsdumper2.\n\
\n\
  install         - Install tsdumper2 in PREFIX ($(PREFIX))\n\
  uninstall       - Uninstall tsdumper2 from PREFIX\n\
\n\
Cleaning targets:\n\
  clean           - Remove tsdumper2 build files\n\
  distclean       - Remove all build files\n\
\n\
  make V=1          Enable verbose build\n\
  make PREFIX=dir   Set install prefix\n\
  make CROSS=prefix Cross compile\n"
