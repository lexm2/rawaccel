# RawAccel Linux Driver

## Userspace Driver

RawAccel for Linux uses a pure userspace driver implementation via libevdev + uinput.

**See [userspace/README.md](userspace/README.md) for complete documentation.**

## Quick Start

```bash
# Install
sudo ./install/install.sh

# Run UI
./run.sh
```

## Why Userspace?

✅ No kernel module required
✅ Works with Secure Boot
✅ No kernel headers needed
✅ Cross-kernel compatible
✅ Safer (can't crash kernel)

## Architecture

```
UI (C#) → Backend (C#) → Unix Socket → C Daemon → libevdev + uinput
```

For detailed architecture, installation, and troubleshooting, see [userspace/README.md](userspace/README.md).
