#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <lgpio.h>
#include <uci.h>

const std::string UCI_PACKAGE  = "pi-relay-control";
const std::string STATE_DIR    = "/var/lib/relay_control";
const int DEFAULT_GPIO_PIN     = 5;
const int DEFAULT_PORT         = 7778;

struct Relay {
    int gpioPin;
    int port;
    std::string stateFile;
    bool alwaysOn = false;
};

int gpioHandle = -1;
std::vector<Relay> g_relays;

// Optional override for the gpiochip to use, read from the "globals"
// section's gpio_chip option (bare chip number or full /dev/gpiochipN
// path). Needed on non-Raspberry-Pi OpenWrt targets, where
// findMainGpiochip()'s label matching below won't find anything.
std::string g_gpioChipOverride;

// ── Config (UCI) ─────────────────────────────────────────────────────────────

// Each relay is one anonymous "config relay" section in
// /etc/config/pi-relay-control, with gpio_pin and port options and an
// optional always_on flag. Multiple sections configure multiple
// independently-controlled relays sharing the same gpiochip, each with
// its own TCP port and persisted state file. If no relay sections are
// present, a single relay is configured from the built-in defaults.
//
// "always_on" forces that relay ON every time this daemon starts,
// ignoring whatever state was last persisted -- for a relay that
// should come up energized whenever the router (and this service with
// it) boots, rather than resuming whatever position a client last
// left it in.
void loadConfig() {
    struct uci_context *ctx = uci_alloc_context();
    struct uci_package *pkg = nullptr;

    if (uci_load(ctx, UCI_PACKAGE.c_str(), &pkg) != UCI_OK || !pkg) {
        std::cout << "No UCI config for " << UCI_PACKAGE << ", using defaults." << std::endl;
    } else {
        struct uci_element *e;
        uci_foreach_element(&pkg->sections, e) {
            struct uci_section *s = uci_to_section(e);
            std::string type(s->type);

            if (type == "globals") {
                const char *chip = uci_lookup_option_string(ctx, s, "gpio_chip");
                if (chip) g_gpioChipOverride = chip;
                continue;
            }

            if (type != "relay") continue;

            const char *gpioStr = uci_lookup_option_string(ctx, s, "gpio_pin");
            const char *portStr = uci_lookup_option_string(ctx, s, "port");
            if (!gpioStr || !portStr) {
                std::cerr << "Malformed relay section, gpio_pin and port are both required" << std::endl;
                continue;
            }

            Relay r;
            r.gpioPin = std::atoi(gpioStr);
            r.port = std::atoi(portStr);

            const char *alwaysOnStr = uci_lookup_option_string(ctx, s, "always_on");
            r.alwaysOn = alwaysOnStr && (std::string(alwaysOnStr) == "1" || std::string(alwaysOnStr) == "true");

            r.stateFile = STATE_DIR + "/state_pin" + std::to_string(r.gpioPin);
            std::cout << "Config: relay gpio_pin=" << r.gpioPin << " port=" << r.port
                      << (r.alwaysOn ? " always_on=true" : "") << std::endl;
            g_relays.push_back(r);
        }
    }

    if (pkg) uci_unload(ctx, pkg);
    uci_free_context(ctx);

    if (g_relays.empty()) {
        g_relays.push_back({DEFAULT_GPIO_PIN, DEFAULT_PORT,
                             STATE_DIR + "/state_pin" + std::to_string(DEFAULT_GPIO_PIN)});
    }
}

// ── State persistence ────────────────────────────────────────────────────────

void saveState(const Relay& relay, int state) {
    std::ofstream file(relay.stateFile);
    if (file.is_open()) {
        file << state;
        file.close();
    } else {
        std::cerr << "Failed to save state to " << relay.stateFile << std::endl;
    }
}

int loadState(const Relay& relay) {
    std::ifstream file(relay.stateFile);
    if (!file.is_open()) {
        std::cout << "No state file found for GPIO " << relay.gpioPin << ", defaulting to OFF" << std::endl;
        return 0;
    }
    int state = 0;
    file >> state;
    file.close();
    std::cout << "Restored GPIO " << relay.gpioPin << " state: " << (state ? "ON" : "OFF") << std::endl;
    return state;
}

// ── GPIO helpers ─────────────────────────────────────────────────────────────

// findMainGpiochip finds the /dev/gpiochipN exposing the 40-pin header on
// Raspberry Pi boards, rather than assuming it's chip 0. Which number
// that chip lands on depends on how many other gpiochips (HATs, PMIC,
// SD/ETH housekeeping, etc.) enumerate first. This heuristic only
// recognizes Raspberry Pi's own chip labels ("pinctrl-rp1" on Pi 5,
// "pinctrl-bcm2835" on earlier boards) -- on non-Pi OpenWrt hardware it
// will find nothing and the caller falls back to gpiochip0, which can
// be overridden via the gpio_chip option in the "globals" UCI section
// if that fallback is wrong for a given router.
//
// Queries each chip's label via GPIO_GET_CHIPINFO_IOCTL (the same cdev
// ioctl gpiodetect/libgpiod use) rather than reading /sys/bus/gpio/devices/
// */label -- that sysfs attribute belongs to the legacy, now-deprecated
// sysfs GPIO ABI and isn't guaranteed to exist on current kernels.
int findMainGpiochip() {
    const std::filesystem::path devDir = "/dev";
    if (!std::filesystem::exists(devDir)) return -1;

    const std::string prefix = "gpiochip";
    for (const auto& entry : std::filesystem::directory_iterator(devDir)) {
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0) continue;

        int fd = open(entry.path().c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct gpiochip_info info{};
        int ret = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info);
        close(fd);
        if (ret < 0) continue;

        std::string label(info.label);
        if (label != "pinctrl-rp1" && label != "pinctrl-bcm2835") continue;

        try {
            return std::stoi(name.substr(prefix.size()));
        } catch (...) {
            return -1;
        }
    }
    return -1;
}

// Opens the shared gpiochip once and claims every configured relay's pin
// as an output on it -- one chip handle covers all lines on the header,
// so relays don't each need their own chip open.
void setup() {
    system(("mkdir -p " + STATE_DIR).c_str());

    int chipNum;
    if (!g_gpioChipOverride.empty()) {
        const std::string prefix = "/dev/gpiochip";
        std::string numPart = g_gpioChipOverride.rfind(prefix, 0) == 0
            ? g_gpioChipOverride.substr(prefix.size())
            : g_gpioChipOverride;
        try {
            chipNum = std::stoi(numPart);
        } catch (...) {
            std::cerr << "Invalid gpio_chip override \"" << g_gpioChipOverride
                      << "\", falling back to gpiochip0" << std::endl;
            chipNum = 0;
        }
        std::cout << "Using configured gpio_chip override: gpiochip" << chipNum << std::endl;
    } else {
        chipNum = findMainGpiochip();
        if (chipNum < 0) {
            std::cerr << "Could not find a gpiochip labeled pinctrl-rp1 or pinctrl-bcm2835, "
                          "falling back to gpiochip0 -- on non-Raspberry-Pi hardware, set "
                          "gpio_chip in the globals section of /etc/config/pi-relay-control "
                          "instead of relying on this autodetection." << std::endl;
            chipNum = 0;
        } else {
            std::cout << "Found header gpiochip at /dev/gpiochip" << chipNum << std::endl;
        }
    }

    gpioHandle = lgGpiochipOpen(chipNum);
    if (gpioHandle < 0) {
        std::cerr << "Failed to open GPIO chip " << chipNum << std::endl;
        return;
    }

    for (const auto& relay : g_relays) {
        int lastState;
        if (relay.alwaysOn) {
            lastState = 1;
            std::cout << "GPIO " << relay.gpioPin << " is always_on, forcing ON at startup" << std::endl;
            saveState(relay, lastState);
        } else {
            lastState = loadState(relay);
        }
        lgGpioClaimOutput(gpioHandle, 0, relay.gpioPin, lastState);
        std::cout << "GPIO " << relay.gpioPin << " ready, state="
                  << (lastState ? "ON" : "OFF") << std::endl;
    }
}

void cleanup() {
    if (gpioHandle >= 0) {
        for (const auto& relay : g_relays)
            lgGpioWrite(gpioHandle, relay.gpioPin, 0);
        lgGpiochipClose(gpioHandle);
    }
}

void setRelay(const Relay& relay, int state) {
    lgGpioWrite(gpioHandle, relay.gpioPin, state);
    saveState(relay, state);
}

// ── Socket server ────────────────────────────────────────────────────────────

void handleClient(int clientFd, const Relay& relay) {
    char buf[256] = {};

    int n = recv(clientFd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(clientFd); return; }

    std::string cmd(buf);
    cmd.erase(std::remove_if(cmd.begin(), cmd.end(), [](char c){ return c == '\n' || c == '\r'; }), cmd.end());

    std::string response;

    if (cmd == "on") {
        setRelay(relay, 1);
        response = "OK RELAY=ON\n";

    } else if (cmd == "off") {
        setRelay(relay, 0);
        response = "OK RELAY=OFF\n";

    } else if (cmd == "status") {
        int val = lgGpioRead(gpioHandle, relay.gpioPin);
        response = "RELAY=" + std::string(val ? "ON" : "OFF") + "\n";

    } else {
        response = "ERR unknown command. Use: on | off | status\n";
    }

    std::cout << "GPIO " << relay.gpioPin << " CMD: " << cmd << " -> " << response;
    send(clientFd, response.c_str(), response.size(), 0);
    close(clientFd);
}

void runRelayServer(Relay relay) {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(relay.port);

    if (bind(serverFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind port " << relay.port << " for GPIO " << relay.gpioPin << std::endl;
        return;
    }
    listen(serverFd, 5);

    std::cout << "GPIO " << relay.gpioPin << " relay listening on port " << relay.port << std::endl;

    while (true) {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd >= 0)
            handleClient(clientFd, relay);
    }
}

int main() {
    loadConfig();
    setup();

    std::vector<std::thread> servers;
    for (const auto& relay : g_relays)
        servers.emplace_back(runRelayServer, relay);

    for (auto& t : servers)
        t.join();

    cleanup();
    return 0;
}
