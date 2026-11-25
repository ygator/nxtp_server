# NXTP Raspberry Pi server Makefile
#
# Build:             make
# Install + start:   sudo make install
# Remove service:    sudo make uninstall
# Remove build:      make clean
#
# Override settings if required, e.g.:
#   make PORT=12300

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -D_DEFAULT_SOURCE
LDFLAGS  ?=
LDLIBS   ?=

TARGET       := nxtp_server
SOURCE       := nxtp_server.c
SERVICE      := nxtp-server.service
PORT         ?= 12300

PREFIX       ?= /usr/local
SBINDIR      ?= $(PREFIX)/sbin
SYSTEMD_DIR  ?= /etc/systemd/system
BIN_PATH     := $(DESTDIR)$(SBINDIR)/$(TARGET)
UNIT_PATH    := $(DESTDIR)$(SYSTEMD_DIR)/$(SERVICE)

SERVICE_USER  ?= nxtp
SERVICE_GROUP ?= nxtp

.PHONY: all install uninstall clean status logs enable disable help

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

# Installation changes system files and therefore requires root.
install: all
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "Please run: sudo make install"; \
		exit 1; \
	fi
	@command -v systemctl >/dev/null 2>&1 || { echo "systemd/systemctl not found"; exit 1; }
	@id -u $(SERVICE_USER) >/dev/null 2>&1 || \
		useradd --system --no-create-home --shell /usr/sbin/nologin $(SERVICE_USER)
	@install -d -o root -g root -m 0755 $(DESTDIR)$(SBINDIR)
	@install -o root -g root -m 0755 $(TARGET) $(BIN_PATH)
	@install -o root -g root -m 0644 $(SERVICE) $(UNIT_PATH)
	@systemctl daemon-reload
	@systemctl enable --now $(SERVICE)
	@echo "NXTP server installed and started."
	@systemctl --no-pager --full status $(SERVICE) || true

# Stop/disable the service and remove its installed unit and executable.
# The service account is intentionally retained in case it owns log or
# configuration files added by a future version.
uninstall:
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "Please run: sudo make uninstall"; \
		exit 1; \
	fi
	@if command -v systemctl >/dev/null 2>&1; then \
		systemctl disable --now $(SERVICE) 2>/dev/null || true; \
	fi
	@rm -f $(UNIT_PATH) $(BIN_PATH)
	@if command -v systemctl >/dev/null 2>&1; then systemctl daemon-reload; systemctl reset-failed $(SERVICE) 2>/dev/null || true; fi
	@echo "NXTP server service and installed executable removed."
	@echo "The '$(SERVICE_USER)' system account was retained."

clean:
	rm -f $(TARGET) *.o

status:
	systemctl --no-pager --full status $(SERVICE)

logs:
	@echo "Live Logs:    sudo journalctl -u nxtp-server -f"
	@echo "All Logs:     sudo journalctl -u nxtp-server"
	@echo "Current Boot: sudo journalctl -u nxtp-server -b"
	@echo "Last 100:     sudo journalctl -u nxtp-server -n 100"
	@echo ""
	journalctl -u $(SERVICE) -f

check:
	sudo systemctl status $(SERVICE)

enable:
	sudo systemctl enable --now $(SERVICE)

disable:
	sudo systemctl disable --now $(SERVICE)

help:
	@echo "Targets:"
	@echo "  make             Compile the server"
	@echo "  sudo make install   Install binary and systemd service; enable and start it"
	@echo "  sudo make uninstall Stop/disable service and remove installed files"
	@echo "  make clean       Remove locally compiled files"
	@echo "  make status      Show service status"
	@echo "  make logs        Follow service journal"
	@echo "  make check       Check service status"
