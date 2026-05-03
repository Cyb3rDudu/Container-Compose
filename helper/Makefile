HELPER_NAME        := container-compose-dns-helper
HELPER_INSTALL_DIR := /usr/local/libexec
HELPER_INSTALL_PATH := $(HELPER_INSTALL_DIR)/$(HELPER_NAME)
HELPER_BIN := $(HELPER_NAME)
HELPER_SRC := dns-helper.c

.PHONY: all helper install-helper uninstall-helper clean check-installed

all: helper

helper: $(HELPER_BIN)

$(HELPER_BIN): $(HELPER_SRC)
	clang -O2 -Wall -Wextra -Werror -o $@ $<

install-helper: $(HELPER_BIN)
	@echo "Installing $(HELPER_BIN) as setuid root at $(HELPER_INSTALL_PATH)."
	@echo "This requires sudo. Source: $(abspath $(HELPER_SRC))"
	sudo mkdir -p $(HELPER_INSTALL_DIR)
	sudo install -o root -g wheel -m 4755 $(HELPER_BIN) $(HELPER_INSTALL_PATH)
	@echo
	@echo "Installed. Verify:"
	@ls -la $(HELPER_INSTALL_PATH)
	@echo
	@echo "Mode should be -rwsr-xr-x and owner root:wheel."

uninstall-helper:
	@echo "Removing $(HELPER_INSTALL_PATH) (requires sudo)."
	sudo rm -f $(HELPER_INSTALL_PATH)

check-installed:
	@if [ -x $(HELPER_INSTALL_PATH) ]; then \
		ls -la $(HELPER_INSTALL_PATH); \
	else \
		echo "Not installed. Run: make install-helper"; \
		exit 1; \
	fi

clean:
	rm -f $(HELPER_BIN)
