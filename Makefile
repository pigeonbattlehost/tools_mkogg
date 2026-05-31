# =============================================================================
# mkaudio — static build Makefile
# Скачивает и собирает все зависимости из исходников, линкует статически.
#
# Требования на хосте (только инструменты сборки, не либы):
#   gcc make cmake nasm curl tar pkg-config python3 autoconf automake libtool
#
# Debian/Ubuntu:
#   sudo apt install build-essential cmake nasm curl autoconf automake libtool python3
#
# Использование:
#   make          — скачать, собрать либы, собрать mkaudio
#   make clean    — удалить промежуточные файлы
#   make distclean — удалить всё включая скачанные исходники
# =============================================================================

# ---------- версии библиотек ------------------------------------------------
OGG_VER     = 1.3.5
VORBIS_VER  = 1.3.7
LAME_VER    = 3.100
FLAC_VER    = 1.4.3
FDK_VER     = 2.0.3

# ---------- пути ------------------------------------------------------------
ROOT        = $(CURDIR)
DEPS        = $(ROOT)/deps
SRC         = $(DEPS)/src
PREFIX      = $(DEPS)/install

OGG_SRC     = $(SRC)/libogg-$(OGG_VER)
VORBIS_SRC  = $(SRC)/libvorbis-$(VORBIS_VER)
LAME_SRC    = $(SRC)/lame-$(LAME_VER)
FLAC_SRC    = $(SRC)/flac-$(FLAC_VER)
FDK_SRC     = $(SRC)/fdk-aac-$(FDK_VER)

# ---------- флаги компилятора -----------------------------------------------
CC          = gcc
CFLAGS      = -O2 -I$(PREFIX)/include
LDFLAGS     = -L$(PREFIX)/lib
LIBS        = \
    $(PREFIX)/lib/libvorbisenc.a \
    $(PREFIX)/lib/libvorbis.a    \
    $(PREFIX)/lib/libogg.a       \
    $(PREFIX)/lib/libFLAC.a      \
    $(PREFIX)/lib/libmp3lame.a   \
    $(PREFIX)/lib/libfdk-aac.a   \
    -lm -lpthread

DEFINES     = -DHAVE_VORBIS -DHAVE_FLAC -DHAVE_LAME -DHAVE_FDK_AAC

# =============================================================================
.PHONY: all clean distclean \
        libogg libvorbis liblame libflac libfdk

all: mkaudio

mkaudio: mkaudio.c \
         $(PREFIX)/lib/libogg.a \
         $(PREFIX)/lib/libvorbisenc.a \
         $(PREFIX)/lib/libmp3lame.a \
         $(PREFIX)/lib/libFLAC.a \
         $(PREFIX)/lib/libfdk-aac.a
	$(CC) $(CFLAGS) $(DEFINES) -o $@ mkaudio.c $(LDFLAGS) $(LIBS)
	@echo ""
	@echo "==> mkaudio собран: $(ROOT)/mkaudio"
	@ls -lh mkaudio

# =============================================================================
# libogg
# =============================================================================
$(PREFIX)/lib/libogg.a: $(OGG_SRC)/configure
	cd $(OGG_SRC) && ./configure \
	    --prefix=$(PREFIX) \
	    --enable-static \
	    --disable-shared \
	    --disable-dependency-tracking
	$(MAKE) -C $(OGG_SRC) install
	@echo "==> libogg installed"

$(OGG_SRC)/configure: | $(SRC)
	cd $(SRC) && \
	    curl -fsSL "https://downloads.xiph.org/releases/ogg/libogg-$(OGG_VER).tar.gz" \
	    | tar xz
	@echo "==> libogg extracted"

# =============================================================================
# libvorbis (зависит от libogg)
# =============================================================================
$(PREFIX)/lib/libvorbisenc.a: $(VORBIS_SRC)/configure $(PREFIX)/lib/libogg.a
	cd $(VORBIS_SRC) && ./configure \
	    --prefix=$(PREFIX) \
	    --enable-static \
	    --disable-shared \
	    --disable-dependency-tracking \
	    --with-ogg=$(PREFIX)
	$(MAKE) -C $(VORBIS_SRC) install
	@echo "==> libvorbis installed"

$(VORBIS_SRC)/configure: | $(SRC)
	cd $(SRC) && \
	    curl -fsSL "https://downloads.xiph.org/releases/vorbis/libvorbis-$(VORBIS_VER).tar.gz" \
	    | tar xz
	@echo "==> libvorbis extracted"

# =============================================================================
# libmp3lame
# =============================================================================
$(PREFIX)/lib/libmp3lame.a: $(LAME_SRC)/configure
	cd $(LAME_SRC) && ./configure \
	    --prefix=$(PREFIX) \
	    --enable-static \
	    --disable-shared \
	    --disable-dependency-tracking \
	    --disable-frontend
	$(MAKE) -C $(LAME_SRC) install
	@echo "==> libmp3lame installed"

$(LAME_SRC)/configure: | $(SRC)
	cd $(SRC) && \
	    curl -fsSL "https://sourceforge.net/projects/lame/files/lame/$(LAME_VER)/lame-$(LAME_VER).tar.gz/download" \
	    -o lame-$(LAME_VER).tar.gz && \
	    tar xzf lame-$(LAME_VER).tar.gz
	@echo "==> lame extracted"

# =============================================================================
# libFLAC
# =============================================================================
$(PREFIX)/lib/libFLAC.a: $(FLAC_SRC)/configure $(PREFIX)/lib/libogg.a
	cd $(FLAC_SRC) && ./configure \
	    --prefix=$(PREFIX) \
	    --enable-static \
	    --disable-shared \
	    --disable-dependency-tracking \
	    --with-ogg=$(PREFIX) \
	    --disable-programs \
	    --disable-examples \
	    --disable-doxygen-docs
	$(MAKE) -C $(FLAC_SRC) install
	@echo "==> libFLAC installed"

$(FLAC_SRC)/configure: | $(SRC)
	cd $(SRC) && \
	    curl -fsSL "https://github.com/xiph/flac/releases/download/$(FLAC_VER)/flac-$(FLAC_VER).tar.xz" \
	    | tar xJ
	@echo "==> libFLAC extracted"

# =============================================================================
# libfdk-aac
# =============================================================================
$(PREFIX)/lib/libfdk-aac.a: $(FDK_SRC)/CMakeLists.txt
	cmake -S $(FDK_SRC) -B $(FDK_SRC)/build \
	    -DCMAKE_INSTALL_PREFIX=$(PREFIX) \
	    -DBUILD_SHARED_LIBS=OFF \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_C_FLAGS="-O2" \
	    -DCMAKE_CXX_FLAGS="-O2"
	cmake --build   $(FDK_SRC)/build --parallel
	cmake --install $(FDK_SRC)/build
	@echo "==> libfdk-aac installed"

$(FDK_SRC)/CMakeLists.txt: | $(SRC)
	cd $(SRC) && \
	    curl -fsSL "https://github.com/mstorsjo/fdk-aac/archive/refs/tags/v$(FDK_VER).tar.gz" \
	    | tar xz
	@echo "==> libfdk-aac extracted"

# =============================================================================
# служебное
# =============================================================================
$(SRC):
	mkdir -p $(SRC)

clean:
	rm -f mkaudio
	@echo "Бинарь удалён. Исходники либ в deps/ не тронуты."

distclean: clean
	rm -rf deps/
	@echo "Всё удалено."
