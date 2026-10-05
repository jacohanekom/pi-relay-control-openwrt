# pi-relay-control-openwrt

OpenWrt packaging of the [pi-relay-control](https://github.com/jacohanekom/pi-relay-control)
GPIO relay daemon, plus a LuCI web UI. Exposes a TCP socket interface to
turn one or more relays on/off and query their state, with persistent
state across restarts -- same protocol as the
[Debian/systemd](https://github.com/jacohanekom/pi-relay-control) and
[Alpine/OpenRC](https://github.com/jacohanekom/pi-relay-control-alpine)
siblings. The only real difference is config format: this port uses
[UCI](https://openwrt.org/docs/guide-user/base-system/uci)
(`/etc/config/pi-relay-control`) instead of the flat
`/etc/pi-relay-control.conf` the other two use, so it's editable from
LuCI and `uci`/`uci-defaults` like any other OpenWrt service.

This repo holds three packages, meant to be added as a custom feed
rather than built standalone:

- `liblgpio` -- same `liblgpio.so.1` the Alpine port ships, built from
  the same pinned [joan2937/lg](https://github.com/joan2937/lg) commit.
  lgpio isn't packaged for OpenWrt (and OpenWrt's packaged alternative,
  `libgpiod`, currently fails to build on every stable release branch --
  an unrelated upstream Python-bindings bug, fixed on `master` but not
  yet backported), so this vendors it from source instead, same as the
  Alpine port already does.
- `pi-relay-control` -- the daemon, init script, and default UCI config.
- `luci-app-pi-relay-control` -- a LuCI page (`Services -> Relay
  Control`) that edits the UCI config and shows live on/off/status per
  relay with immediate-effect toggle buttons.

## Requirements

- An OpenWrt target with GPIO lines available on a `/dev/gpiochipN`
  (anything the mainline kernel's `gpio-cdev` ABI covers)
- One or more relays, each on its own GPIO pin (configurable)
- The `luci` feed enabled (default in `feeds.conf.default`) -- needed to
  build `luci-app-pi-relay-control`, which includes `feeds/luci/luci.mk`

## Building

Add this repo as a custom feed in your OpenWrt buildroot's
`feeds.conf.default` (or `feeds.conf`):

```
src-link pi-relay-control /path/to/pi-relay-control-openwrt
```

Then:

```sh
./scripts/feeds update pi-relay-control
./scripts/feeds install -a -p pi-relay-control

make menuconfig
# Libraries  --->  <*> liblgpio
# Utilities  --->  <*> pi-relay-control
# LuCI  --->  3. Applications  --->  <*> luci-app-pi-relay-control

make package/liblgpio/compile V=s
make package/pi-relay-control/compile V=s
make package/luci-app-pi-relay-control/compile V=s
```

All three `.apk`s (OpenWrt packages as `apk`, not `opkg`, as of 25.12)
land in `bin/packages/<arch>/pi-relay-control/`.

## Configuration

Edit `/etc/config/pi-relay-control` (or use LuCI's `Services -> Relay
Control` page), one `config relay` section per relay:

```
config relay
	option gpio_pin	'4'
	option port	'7778'
	option always_on '1'

config relay
	option gpio_pin	'2'
	option port	'7779'
```

Each relay runs its own TCP listener on its own port, so ports must be
unique. The daemon claims all configured pins on the same gpiochip at
startup. If no `relay` sections are present, it falls back to a single
relay on GPIO 5 / port 7778.

`always_on` forces that relay ON every time the daemon starts (and
persists that as its saved state), overriding whatever a client last
left it as. Without it, each relay resumes whatever state was last
saved to `/var/lib/relay_control/state_pin<N>`, defaulting to OFF if no
state file exists yet.

### GPIO chip detection

The daemon tries to find the gpiochip exposing the Raspberry Pi 40-pin
header by its kernel label (`pinctrl-rp1` on Pi 5, `pinctrl-bcm2835` on
earlier Pi boards), falling back to `gpiochip0` if neither is found --
which on most non-Raspberry-Pi OpenWrt targets is the wrong chip, or
simply not where the relay is wired. Override it with an explicit
`globals` section:

```
config globals 'globals'
	option gpio_chip '0'      # bare chip number, or e.g. '/dev/gpiochip4'
```

Restart after any config change: `/etc/init.d/pi-relay-control reload`
(LuCI's "Save & Apply" does this automatically via a UCI reload
trigger).

## Usage

Control a relay with any TCP client, e.g. `nc`, against the port
assigned to it in the config:

```sh
echo "on"     | nc localhost 7778    # Turn relay ON  -> OK RELAY=ON
echo "off"    | nc localhost 7778    # Turn relay OFF -> OK RELAY=OFF
echo "status" | nc localhost 7778    # Query state    -> RELAY=ON
```

### Commands

| Command  | Response                                      |
|----------|------------------------------------------------|
| `on`     | `OK RELAY=ON`                                   |
| `off`    | `OK RELAY=OFF`                                  |
| `status` | `RELAY=ON` or `RELAY=OFF`                       |
| other    | `ERR unknown command. Use: on \| off \| status` |

## LuCI web UI

`luci-app-pi-relay-control` adds a `Services -> Relay Control` page
with:

- A live status table (current on/off state per relay, fetched via a
  `luci.pi-relay-control` rpcd backend that talks to the daemon's TCP
  ports), with immediate-effect On/Off buttons that don't require Save
  & Apply or a restart.
- A standard UCI form below it for adding/removing relays and editing
  `gpio_pin` / `port` / `always_on`, which does require Save & Apply
  (triggers an automatic service reload).

The rpcd backend (`/usr/libexec/rpcd/luci.pi-relay-control`) shells out
to BusyBox `nc` to speak the daemon's TCP protocol, so it needs the
`nc` applet enabled in BusyBox -- the default on stock OpenWrt builds.

## Service management

```sh
/etc/init.d/pi-relay-control start
/etc/init.d/pi-relay-control stop
/etc/init.d/pi-relay-control reload    # re-reads UCI config
/etc/init.d/pi-relay-control enable    # start on boot
/etc/init.d/pi-relay-control status
```

Runs under `procd`, which respawns it automatically on failure (5 s
delay, unlimited retries), and persists each relay's state to
`/var/lib/relay_control/state_pin<N>` (one file per configured GPIO
pin) so every relay returns to its last position after a reboot.
