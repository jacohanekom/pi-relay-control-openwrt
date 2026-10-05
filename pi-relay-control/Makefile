include $(TOPDIR)/rules.mk

PKG_NAME:=pi-relay-control
PKG_VERSION:=1.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=Jaco Hanekom <jhanekom@gmail.com>
PKG_LICENSE:=custom

include $(INCLUDE_DIR)/package.mk

define Package/pi-relay-control
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=GPIO relay control daemon
  URL:=https://github.com/jacohanekom/pi-relay-control-openwrt
  DEPENDS:=+liblgpio +libuci +libstdcpp +libpthread
endef

define Package/pi-relay-control/description
  Controls one or more relays via GPIO (through liblgpio) with a TCP
  socket interface and state persistence. Restores each relay's last
  known state on startup, or forces it ON at every startup via the
  always_on UCI option. Relays, GPIO pins, and ports are configured in
  /etc/config/pi-relay-control.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/main.cpp $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 -Wall -O2 -pthread \
		$(TARGET_CPPFLAGS) \
		-o $(PKG_BUILD_DIR)/relay_control $(PKG_BUILD_DIR)/main.cpp \
		$(TARGET_LDFLAGS) -llgpio -luci
endef

define Package/pi-relay-control/conffiles
/etc/config/pi-relay-control
endef

define Package/pi-relay-control/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/relay_control $(1)/usr/bin/relay_control

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/pi-relay-control.init $(1)/etc/init.d/pi-relay-control

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/pi-relay-control.config $(1)/etc/config/pi-relay-control

	$(INSTALL_DIR) $(1)/var/lib/relay_control
endef

$(eval $(call BuildPackage,pi-relay-control))
