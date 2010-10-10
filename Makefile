# $Id$

BRCM_VERSION=2
BRCM_RELEASE=14
BRCM_EXTRAVERSION=02

BRCM_RELEASETAG=$(BRCM_VERSION).$(BRCM_RELEASE)L.$(BRCM_EXTRAVERSION)


###########################################
#
# Define Basic Variables
#
###########################################
BUILD_DIR = $(shell pwd)
INC_KERNEL_BASE = $(BUILD_DIR)/kernel_2_4_17
KERNEL_DIR = $(INC_KERNEL_BASE)/linux
USERAPPS_DIR = $(BUILD_DIR)/userapps
LINUXDIR = $(INC_KERNEL_BASE)/linux
HOSTTOOLS_DIR = $(BUILD_DIR)/hostTools
IMAGES_DIR = $(BUILD_DIR)/images
TARGETS_DIR = $(BUILD_DIR)/targets
DEFAULTCFG_DIR = $(TARGETS_DIR)/defaultcfg
FSSRC_DIR = $(TARGETS_DIR)/fs.src
CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
          else if [ -x /bin/bash ]; then echo /bin/bash; \
          else echo sh; fi ; fi)
RUN_NOISE=0


###########################################
TOOLCHAIN=/opt/toolchains/uclibc
CROSS_COMPILE=mips-uclibc-

AR              = $(CROSS_COMPILE)ar
AS              = $(CROSS_COMPILE)as
LD              = $(CROSS_COMPILE)ld
CC              = $(CROSS_COMPILE)gcc
CXX             = $(CROSS_COMPILE)g++
CPP             = $(CROSS_COMPILE)cpp
NM              = $(CROSS_COMPILE)nm
STRIP           = $(CROSS_COMPILE)strip
OBJCOPY         = $(CROSS_COMPILE)objcopy
OBJDUMP         = $(CROSS_COMPILE)objdump
RANLIB          = $(CROSS_COMPILE)ranlib

LIB_PATH        = $(TOOLCHAIN)/mips-linux/lib
LIBDIR          = $(TOOLCHAIN)/mips-linux/lib
LIBCDIR         = $(TOOLCHAIN)/mips-linux

###########################################
#
# Application-specific settings
#
###########################################
PROFILE=LIVEBOX
INSTALL_DIR = $(TARGETS_DIR)/fs.src
TARGET_FS = $(TARGETS_DIR)/$(PROFILE)/fs
PROFILE_DIR = $(TARGETS_DIR)/$(PROFILE)
PROFILE_PATH = $(TARGETS_DIR)/$(PROFILE)/$(PROFILE)
VENDOR_NAME = bcm

###########################################
#
# Complete list of applications
#
###########################################
export OPENSOURCE_DIR=$(USERAPPS_DIR)/opensource
SUBDIRS_OPENSOURCE = \
        $(OPENSOURCE_DIR)/bridge-utils \
        $(OPENSOURCE_DIR)/iptables \
        $(OPENSOURCE_DIR)/busybox \
        $(OPENSOURCE_DIR)/ddns \
        $(OPENSOURCE_DIR)/ntpclient \
        $(OPENSOURCE_DIR)/dropbear \
        $(OPENSOURCE_DIR)/ftpd \
        $(OPENSOURCE_DIR)/ini \
        $(OPENSOURCE_DIR)/mtd \
        $(OPENSOURCE_DIR)/msmtp \
        $(OPENSOURCE_DIR)/wol \
	$(OPENSOURCE_DIR)/lua

export BROADCOM_DIR=$(USERAPPS_DIR)/broadcom

INC_KERNEL_PATH=$(BROADCOM_DIR)/modulesrc/include/asm-mips/bcm963xx
INC_KERNEL_PATH2=$(BROADCOM_DIR)/modulesrc/include/bcm963xx

INVENTEL_DIR=$(USERAPPS_DIR)/inventel
SUBDIRS_INVENTEL = \
	$(INVENTEL_DIR)/bin \
	$(INVENTEL_DIR)/ledctrl \
	$(INVENTEL_DIR)/sendarp

HAH_DIR=$(USERAPPS_DIR)/hah
SUBDIRS_HAH = \
	$(HAH_DIR)/xaplib \
	$(HAH_DIR)/xaplib2 \
	$(HAH_DIR)/xap-hub \
	$(HAH_DIR)/xap-livebox \
	$(HAH_DIR)/xap-snoop \
	$(HAH_DIR)/xap-pachube \
	$(HAH_DIR)/xap-sms \
	$(HAH_DIR)/xap-currentcost \
	$(HAH_DIR)/xap-googlecal \
	$(HAH_DIR)/xap-twitter \
	$(HAH_DIR)/xap-plugboard \
	$(HAH_DIR)/xap-bridge \
	$(HAH_DIR)/xap-serial \
	$(HAH_DIR)/luarex \
	$(HAH_DIR)/klone

SUBDIRS = $(SUBDIRS_BROADCOM) $(SUBDIRS_OPENSOURCE) $(SUBDIRS_INVENTEL) $(SUBDIRS_HAH)

OPENSOURCE_APPS = brctl dropbear ftpd msmtp iptables busybox ntpclient ini mtd lua wol
INVENTEL_APPS = inventelbin sendarp ledctrl
HAH_APPS = xaplib xaplib2 xap-hub xap-livebox xap-snoop xap-pachube xap-sms xap-bridge \
	xap-currentcost xap-googlecal xap-twitter xap-plugboard xap-serial luarex klone

BUSYBOX_DIR = $(OPENSOURCE_DIR)/busybox

BRCMAPPS = $(OPENSOURCE_APPS) $(INVENTEL_APPS) $(HAH_APPS) libc

TGT =  dwbtool cramfs

all: kernelbuild modbuild app tool buildimage

$(KERNEL_DIR)/Image:
	cp $(PROFILE_DIR)/kernel-Config $(KERNEL_DIR)/.config
	cd $(KERNEL_DIR); \
	$(MAKE) oldconfig; \
	$(MAKE) dep && $(MAKE) && \
	$(MAKE) modules && $(MAKE) modules_install

kernelbuild:
ifeq ($(wildcard $(KERNEL_DIR)/Image),)
	cp $(PROFILE_DIR)/kernel-Config $(KERNEL_DIR)/.config
	cd $(KERNEL_DIR); \
	$(MAKE) oldconfig; \
	$(MAKE) dep && $(MAKE)
else
	cd $(KERNEL_DIR); $(MAKE)
endif


kernel: kernelbuild tool buildimage

inventelbin:
	$(MAKE) -C $(INVENTEL_DIR)/bin

modbuild:
	cd $(KERNEL_DIR); $(MAKE) modules && $(MAKE) modules_install

modules: modbuild tool buildimage

app: prebuild $(BRCMAPPS)
# All apps are built we don't need the dev stuff anymore.
	$(RM) -rf $(INSTALL_DIR)/include

prebuild:
	mkdir -p $(INSTALL_DIR)/bin $(INSTALL_DIR)/lib

hah:	$(HAH_APPS)


xaplib:
	$(MAKE) -C $(HAH_DIR)/xaplib
	install -m 755 -d $(INSTALL_DIR)/lib
	install -m 644 $(HAH_DIR)/xaplib/libxap.so $(INSTALL_DIR)/lib
	$(STRIP) $(INSTALL_DIR)/lib/libxap.so
	install -m 755 -d $(INSTALL_DIR)/include
	install -m 644 $(HAH_DIR)/xaplib/*.h $(INSTALL_DIR)/include

xaplib2:
	$(MAKE) -C $(HAH_DIR)/xaplib2
	install -m 755 -d $(INSTALL_DIR)/lib
	install -m 644 $(HAH_DIR)/xaplib2/libxap2.so $(INSTALL_DIR)/lib
	$(STRIP) $(INSTALL_DIR)/lib/libxap.so
	install -m 755 -d $(INSTALL_DIR)/include
	install -m 644 $(HAH_DIR)/xaplib2/*.h $(INSTALL_DIR)/include

xap-hub: xaplib2
	$(MAKE) -C $(HAH_DIR)/xap-hub
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-hub/xap-hub $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-hub

xap-livebox: xaplib2
	$(MAKE) -C $(HAH_DIR)/xap-livebox
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-livebox/xap-livebox $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-livebox

xap-sms: xaplib2
	$(MAKE) -C $(HAH_DIR)/xap-sms
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-sms/xap-sms $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-sms

xap-snoop: xaplib2
	$(MAKE) -C $(HAH_DIR)/xap-snoop
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-snoop/xap-snoop $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-snoop

xap-serial: xaplib2
	$(MAKE) -C $(HAH_DIR)/xap-serial
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-serial/xap-serial $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-serial

xap-pachube: xaplib2 libcurl
	$(MAKE) -C $(HAH_DIR)/xap-pachube
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-pachube/xap-pachube $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-pachube

xap-currentcost: xaplib2 libxml2
	$(MAKE) -C $(HAH_DIR)/xap-currentcost
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-currentcost/xap-currentcost $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-currentcost

xap-bridge: xaplib
	$(MAKE) -C $(HAH_DIR)/xap-bridge
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-bridge/xap-bridge $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-bridge

libopenssl:
	@if [ ! -d $(OPENSOURCE_DIR)/openssl-0.9.8l ]; then \
	   (cd $(OPENSOURCE_DIR); tar zxf openssl-0.9.8l.tar.gz; ln -s openssl-0.9.8l openssl); \
	fi
	if [ ! -f $(OPENSOURCE_DIR)/openssl/Makefile ]; then \
	  (cd $(OPENSOURCE_DIR)/openssl; ./Configure no-hw dist) \
	fi
	$(MAKE) -C $(OPENSOURCE_DIR)/openssl CC=$(CC) AR="$(AR) r" RANLIB=$(RANLIB)
# CURL wants to find them in the lib subdirectory.
	test -d $(OPENSOURCE_DIR)/openssl/lib || mkdir $(OPENSOURCE_DIR)/openssl/lib
	cp $(OPENSOURCE_DIR)/openssl/*.a $(OPENSOURCE_DIR)/openssl/lib

libcurl: libopenssl
	@if [ ! -d $(OPENSOURCE_DIR)/curl-7.19.7 ]; then \
	   (cd $(OPENSOURCE_DIR); tar zxf curl-7.19.7.tar.gz; ln -s curl-7.19.7 curl; rm -f $(OPENSOURCE_DIR)/curl/Makefile) \
	fi
	@if [ ! -f $(OPENSOURCE_DIR)/curl/Makefile ]; then \
	    (cd $(OPENSOURCE_DIR)/curl; ./configure CFLAGS='-Os' LDFLAGS='-Wl,-s -Wl,-Bsymbolic -Wl,--gc-sections' --host=mips-linux --with-ssl=$(OPENSOURCE_DIR)/openssl --with-random=/dev/urandom --disable-manual --disable-static --disable-proxy --enable-optimize --disable-tftp --disable-ftp --disable-dict --disable-ldap --disable-file --disable-telnet --disable-largefile --disable-debug --with-ca-bundle=/etc_ro_fs/curl-ca-bundle.crt --prefix=$(INSTALL_DIR)) \
	fi
# Download the latest CERT bundle
	test -f $(INSTALL_DIR)/etc_ro_fs/curl-ca-bundle.crt || wget -O $(INSTALL_DIR)/etc_ro_fs/curl-ca-bundle.crt http://curl.haxx.se/ca/cacert.pem
	$(MAKE) -C $(OPENSOURCE_DIR)/curl install-strip
	$(RM) -rf $(INSTALL_DIR)/share

libxml2:
	@if [ ! -d $(OPENSOURCE_DIR)/libxml2-2.7.6 ]; then \
	   (cd $(OPENSOURCE_DIR); tar zxf libxml2-2.7.6.tar.gz; ln -s libxml2-2.7.6 libxml2) \
	fi
	@if [ ! -f $(OPENSOURCE_DIR)/libxml2/Makefile ]; then \
	  (cd $(OPENSOURCE_DIR)/libxml2; ./configure --host=mips-linux --disable-static --without-docbook --without-python --without-schematron --without-schemas --without-threads --without-zlib --without-valid --without-pattern --without-legacy --without-xinclude --without-modules --without-catalog --without-ftp --without-http --without-zlib --without-python --without-xptr --without-iso8859x --prefix=$(INSTALL_DIR)) \
	fi
	$(MAKE) -C $(OPENSOURCE_DIR)/libxml2
# install-strip is BROKEN with the above directive.. nice one.  So we do it manually.
	install -m 644 $(OPENSOURCE_DIR)/libxml2/.libs/libxml2.so.2.7.6 $(INSTALL_DIR)/lib
	$(STRIP) $(INSTALL_DIR)/lib/libxml2.so.2.7.6
	test -L $(INSTALL_DIR)/lib/libxml2.so || ln -s libxml2.so.2.7.6 $(INSTALL_DIR)/lib/libxml2.so
	test -L $(INSTALL_DIR)/lib/libxml2.so.2 || ln -s libxml2.so.2.7.6 $(INSTALL_DIR)/lib/libxml2.so.2
	install -m 755 -d $(INSTALL_DIR)/include
	install -m 755 -d $(INSTALL_DIR)/include/libxml2
	cp -r $(OPENSOURCE_DIR)/libxml2/include/libxml $(INSTALL_DIR)/include/libxml2

libgcal: libxml2 libcurl
	@if [ ! -d $(OPENSOURCE_DIR)/libgcal-0.9.3 ]; then \
	   (cd $(OPENSOURCE_DIR); tar jxf libgcal-0.9.3.tar.bz2; ln -s libgcal-0.9.3 libgcal) \
	fi
# GCC3 doesn't like -Wno-pointer-sign so remove it.
	@if [ ! -f $(OPENSOURCE_DIR)/libgcal/Makefile ]; then \
	  (cd $(OPENSOURCE_DIR)/libgcal-0.9.3; LIBCURL_LIBS=-lcurl LIBXML_LIBS=-L$(INSTALL_DIR)/lib LIBXML_CFLAGS="-I$(INSTALL_DIR)/include -I$(INSTALL_DIR)/include/libxml2" ./configure --host=mips-linux --disable-check --disable-static --prefix=$(INSTALL_DIR); mv Makefile Makefile.orig; sed 's/-Wno-pointer-sign//' Makefile.orig > Makefile ) \
	fi
	$(MAKE) -C $(OPENSOURCE_DIR)/libgcal install-strip

xap-googlecal: xaplib2 libgcal libxml2 libcurl
	$(MAKE) -C $(HAH_DIR)/xap-googlecal
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-googlecal/xap-googlecal $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-googlecal

xap-twitter: xaplib2 libcurl
	$(MAKE) -C $(HAH_DIR)/xap-twitter
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/xap-twitter/xap-twitter $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-twitter

xap-plugboard: xaplib libluarex lua
	$(MAKE) -C $(HAH_DIR)/xap-plugboard
	install -m 755 $(HAH_DIR)/xap-plugboard/xap-plugboard $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/xap-plugboard

klone: libcurl
	$(MAKE) -C $(HAH_DIR)/klone
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(HAH_DIR)/klone/kloned $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/kloned

libluarex: lua
	$(MAKE) -C $(HAH_DIR)/libluarex
	install -m 755 $(HAH_DIR)/libluarex/libluarex.a $(INSTALL_DIR)/lib

luarex: lua libluarex
	$(MAKE) -C $(HAH_DIR)/lua
	install -m 755 $(HAH_DIR)/lua/lua $(INSTALL_DIR)/usr/bin
	$(STRIP) $(INSTALL_DIR)/usr/bin/lua

lua:
	$(MAKE) -C $(OPENSOURCE_DIR)/lua posix CC=$(CC)
	install -m 755 -d $(INSTALL_DIR)/lib
	install -m 755 -d $(INSTALL_DIR)/include
	cp $(OPENSOURCE_DIR)/lua/src/*.h $(INSTALL_DIR)/include
	cp $(OPENSOURCE_DIR)/lua/src/*.so $(INSTALL_DIR)/lib

brctl:
	$(MAKE) -C $(OPENSOURCE_DIR)/bridge-utils dynamic

iptables:
	$(MAKE) -C $(OPENSOURCE_DIR)/iptables dynamic

busybox:
	@if [ ! -f $(OPENSOURCE_DIR)/busybox/.config ]; then \
	   cp $(PROFILE_DIR)/busybox-Config $(OPENSOURCE_DIR)/busybox/.config; \
	fi
	$(MAKE) -C $(OPENSOURCE_DIR)/busybox install
	cp -a $(OPENSOURCE_DIR)/busybox/_install/* $(INSTALL_DIR)

libc:
	@if [ ! -f $(INSTALL_DIR)/lib/libc.so ] ; then \
	cp $(LIBDIR)/*.o $(INSTALL_DIR)/lib; \
	cp -a $(LIBDIR)/*.so* $(INSTALL_DIR)/lib; \
	fi

ddns:
	$(MAKE) -C $(OPENSOURCE_DIR)/ddns dynamic

msmtp:
	@if [ ! -f $(OPENSOURCE_DIR)/msmtp/Makefile ]; then \
	   (cd $(OPENSOURCE_DIR)/msmtp; ./configure --host=mips --prefix=$(INSTALL_DIR)); \
	fi
	$(MAKE) -C $(OPENSOURCE_DIR)/msmtp 
	$(MAKE) -C $(OPENSOURCE_DIR)/msmtp install-exec
	$(STRIP) $(INSTALL_DIR)/bin/msmtp

ftpd:
	@if [ ! -f $(OPENSOURCE_DIR)/ftpd/Makefile ]; then \
	   (cd $(OPENSOURCE_DIR)/ftpd; ./configure --host=mips ); \
	fi
	$(MAKE) -C $(OPENSOURCE_DIR)/ftpd 
	$(STRIP) $(OPENSOURCE_DIR)/ftpd/src/pure-ftpd
	install -m 755 -d $(INSTALL_DIR)/usr/sbin
	install -m 755 $(OPENSOURCE_DIR)/ftpd/src/pure-ftpd $(INSTALL_DIR)/usr/sbin

ipkg:
	@if [ ! -f $(OPENSOURCE_DIR)/ipkg/Makefile ]; then \
	   (cd $(OPENSOURCE_DIR)/ipkg; ./configure --host=mips --prefix=$(INSTALL_DIR)); \
	fi
	$(MAKE) -C $(OPENSOURCE_DIR)/ipkg
	$(MAKE) -C $(OPENSOURCE_DIR)/ipkg install-exec
	$(STRIP) $(INSTALL_DIR)/bin/ipkg-cl

dropbear:
	@if [ ! -f $(OPENSOURCE_DIR)/dropbear/Makefile ]; then \
	   (cd $(OPENSOURCE_DIR)/dropbear; ./configure --host=mips --prefix=$(INSTALL_DIR) \
		--disable-largefile --disable-shadow --disable-lastlog \
		--disable-utmp --disable-wtmp --disable-utmpx --disable-wtmpx ); \
	fi
	$(MAKE) -C $(OPENSOURCE_DIR)/dropbear PROGRAMS="dropbear dbclient dropbearkey dropbearconvert scp" PROGRESS_METER=1 MULTI=1
	$(MAKE) -C $(OPENSOURCE_DIR)/dropbear strip
	install -m 755 -d $(INSTALL_DIR)/bin
	install -m 755 -d $(INSTALL_DIR)/sbin
	install -m 755 -d $(INSTALL_DIR)/usr/bin
	install -m 755 $(OPENSOURCE_DIR)/dropbear/dropbearmulti $(INSTALL_DIR)/bin
	rm -f $(INSTALL_DIR)/sbin/dropbear
	ln -s /bin/dropbearmulti $(INSTALL_DIR)/sbin/dropbear
	rm -f $(INSTALL_DIR)/usr/bin/dbclient
	ln -s /bin/dropbearmulti $(INSTALL_DIR)/usr/bin/dbclient
	rm -f $(INSTALL_DIR)/bin/dropbearkey
	ln -s /bin/dropbearmulti $(INSTALL_DIR)/bin/dropbearkey
	rm -f $(INSTALL_DIR)/bin/scp
	ln -s /bin/dropbearmulti $(INSTALL_DIR)/bin/scp
	rm -f $(INSTALL_DIR)/bin/dropbearconvert
	ln -s /bin/dropbearmulti $(INSTALL_DIR)/bin/dropbearconvert

ntpclient:
	$(MAKE) -C $(OPENSOURCE_DIR)/ntpclient dynamic

mtd:
	$(MAKE) -C $(OPENSOURCE_DIR)/mtd dynamic

ini:
	$(MAKE) -C $(OPENSOURCE_DIR)/ini install

wol:
	$(MAKE) -C $(OPENSOURCE_DIR)/wol install

sendarp:
	$(MAKE) -C $(INVENTEL_DIR)/sendarp install

ledctrl:
	$(MAKE) -C $(INVENTEL_DIR)/ledctrl install

tool:
	$(MAKE) -C $(HOSTTOOLS_DIR) $(TGT)

buildimage: $(KERNEL_DIR)/Image tool
	rm -f $(INSTALL_DIR)/lib/*.a
	find $(INSTALL_DIR)/lib -name '*so*' -type f | xargs $(STRIP)
	su --command="cd $(TARGETS_DIR); ./buildFS"
	cp $(KERNEL_DIR)/Image $(TARGET_FS)
	mkdir -p $(IMAGES_DIR)
	$(HOSTTOOLS_DIR)/mkcramfs $(TARGET_FS) $(IMAGES_DIR)/Image.bin
	$(HOSTTOOLS_DIR)/dwbtool -c $(IMAGES_DIR)/hah-firmware.dwb $(PROFILE_DIR)/image.script $(IMAGES_DIR)/Image.bin
	cp $(TARGETS_DIR)/fs.src/etc_ro_fs/build $(IMAGES_DIR)
	@if [ -d /tftpboot/inventel/blue_5g ]; then \
	echo Setup for TFTP; \
	cp $(IMAGES_DIR)/hah-firmware.dwb /tftpboot/inventel/blue_5g; \
	fi
	@echo
	@echo -e "Done! Firmware has been built in $(IMAGES_DIR)"

subdirs: $(patsubst %, _dir_%, $(SUBDIRS))

$(patsubst %, _dir_%, $(SUBDIRS)) :
	@if [ -f $(patsubst _dir_%,%,$@)/Makefile ]; then \
	$(MAKE) -C $(patsubst _dir_%,%,$@) $(TGT); \
	fi

clean: app_clean kernel_clean target_clean hosttools_clean
	rm -fr $(INSTALL_DIR)/usr/bin
	rm -fr $(INSTALL_DIR)/usr/sbin
	rm -fr $(INSTALL_DIR)/bin
	rm -fr $(INSTALL_DIR)/sbin
	rm -f $(INSTALL_DIR)/lib/*.*
	rm -f $(IMAGES_DIR)/*
	rm -rf $(OPENSOURCE_DIR)/curl-7.19.7 $(OPENSOURCE_DIR)/curl
	rm -rf $(OPENSOURCE_DIR)/libxml2-2.7.6 $(OPENSOURCE_DIR)/libxml2
	rm -rf $(OPENSOURCE_DIR)/openssl-0.9.8l $(OPENSOURCE_DIR)/openssl
	rm -rf $(OPENSOURCE_DIR)/libgcal-0.9.3 $(OPENSOURCE_DIR)/libgcal

kernel_clean:
	cp $(PROFILE_DIR)/kernel-Config $(KERNEL_DIR)/.config
	$(MAKE) -C $(KERNEL_DIR) mrproper
	rm -f $(KERNEL_DIR)/arch/mips/defconfig
	rm -f $(TARGETS_DIR)/*.o $(TARGETS_DIR)/.*.flags $(TARGETS_DIR)/.depend
	rm -f $(KERNEL_DIR)/Image

app_clean:
# This is UGLY but klone just won't play nice on a clean
# So until I figure out a better idea this will do.
	mv $(HAH_DIR)/klone/Makefile $(HAH_DIR)/klone/Makefile-ignore
	$(MAKE) subdirs TGT=clean
	mv $(HAH_DIR)/klone/Makefile-ignore $(HAH_DIR)/klone/Makefile 
	find $(INSTALL_DIR) -type l -ilname '*busybox' -exec rm {} \;
	$(RM) -rf $(INSTALL_DIR)/include

target_clean:
	rm -f $(PROFILE_DIR)/rootfs.img
	rm -f $(PROFILE_DIR)/Image
	su --command="rm -fr $(TARGET_FS)"

hosttools_clean:
	$(MAKE) -C $(HOSTTOOLS_DIR) clean

export PROFILE_DIR INSTALL_DIR USERAPPS_DIR HOSTTOOLS_DIR BUSYBOX_DIR CROSS_COMPILE TOOLCHAIN \
	AS LD CC CXX AR NM RANLIB STRIP OBJCOPY OBJDUMP LIB_PATH LIBCDIR LIBDIR PROFILE \
	LINUXDIR KERNEL_DIR FSSRC_DIR INC_KERNEL_PATH
