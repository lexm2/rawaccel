.PHONY: all build install uninstall clean test run

all: build

build:
	cargo build --release

install: build
	install -D -m 755 target/release/rawaccel-daemon /usr/local/bin/rawaccel-daemon
	install -D -m 644 systemd/rawaccel.service /etc/systemd/system/rawaccel.service
	systemctl daemon-reload
	@echo "Installation complete. Start with: sudo systemctl start rawaccel"

uninstall:
	systemctl stop rawaccel || true
	systemctl disable rawaccel || true
	rm -f /usr/local/bin/rawaccel-daemon
	rm -f /etc/systemd/system/rawaccel.service
	systemctl daemon-reload

test:
	cargo test

run:
	cargo run

clean:
	cargo clean

fmt:
	cargo fmt

clippy:
	cargo clippy -- -D warnings
