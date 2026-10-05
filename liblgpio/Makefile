include $(TOPDIR)/rules.mk

PKG_NAME:=liblgpio
PKG_VERSION:=bcccd782eceedc5b278b3056ea81d5fbbb89c489
PKG_RELEASE:=1

# lgpio isn't packaged for OpenWrt, so it's fetched and built from
# source here, same pinned commit as the Alpine port's APKBUILD (which
# this mirrors -- see ../pi-relay-control/Makefile).
PKG_SOURCE:=$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/joan2937/lg/archive
PKG_HASH:=1c1069fe27ccf3d1b2a2f45de10d7bf045206f3bcf60460278a514d4dd223999
PKG_BUILD_DIR:=$(BUILD_DIR)/lg-$(PKG_VERSION)

PKG_MAINTAINER:=Jaco Hanekom <jhanekom@gmail.com>
PKG_LICENSE:=custom

include $(INCLUDE_DIR)/package.mk

define Package/liblgpio
  SECTION:=libs
  CATEGORY:=Libraries
  TITLE:=Linux GPIO character-device library (lg)
  URL:=https://github.com/joan2937/lg
endef

define Package/liblgpio/description
  C library for the Linux GPIO character device (lg), used by
  pi-relay-control. Not packaged upstream for OpenWrt, so this builds
  it directly from the joan2937/lg source instead of depending on a
  feed package.
endef

# Only the "lib" target -- skips rgpiod/rgs (which we don't use) and
# their man pages/docs, which need python3/swig that aren't otherwise
# needed here.
define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) lib \
		CROSS_COMPILE="$(TARGET_CROSS)" \
		CFLAGS="$(TARGET_CFLAGS) $(TARGET_CPPFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

# liblgpio.so.1 is the real file the binary's SONAME points at; the
# unversioned liblgpio.so symlink is only needed at link time (shipped
# via Build/InstallDev into staging, not into the runtime package).
define Package/liblgpio/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/liblgpio.so.1 $(1)/usr/lib/
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/lgpio.h $(1)/usr/include/

	$(INSTALL_DIR) $(1)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/liblgpio.so* $(1)/usr/lib/
endef

$(eval $(call BuildPackage,liblgpio))
