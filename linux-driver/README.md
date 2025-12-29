# RawAccel Linux Kernel Driver

LUT-based mouse acceleration driver for Linux.

## Build Requirements

```bash
sudo apt-get update
sudo apt-get install build-essential linux-headers-$(uname -r)
```

## Building

```bash
make
```

## Loading the Module

```bash
sudo insmod rawaccel.ko
```

Verify it loaded:
```bash
lsmod | grep rawaccel
dmesg | tail
```

Check that `/dev/rawaccel` exists:
```bash
ls -l /dev/rawaccel
```

## Testing IOCTL Interface

Compile the test program:
```bash
gcc -o test_ioctl test_ioctl.c
```

Run it:
```bash
./test_ioctl
```

Expected output:
```
RawAccel IOCTL Test Program
============================

✓ Successfully opened /dev/rawaccel
✓ GET_VERSION IOCTL succeeded
  Driver version: 1.0.0

All tests passed!
```

## Unloading the Module

```bash
sudo rmmod rawaccel
```

## Current Implementation Status

### Phase 1: Core Infrastructure (Nearly Complete!)
- [x] Module skeleton (init/exit)
- [x] Makefile and Kbuild
- [x] DKMS configuration
- [x] Basic data structures (rawaccel_types.h)
- [x] misc device registration (/dev/rawaccel)
- [x] Basic IOCTL interface (GET_VERSION)
- [x] input_handler registration
- [x] Device connect/disconnect callbacks
- [x] Event interception and buffering
- [x] Per-device context with acceleration state
- [x] LUT lookup implementation (binary search + interpolation)
- [x] Fixed-point math (16.16 format)
- [x] Euclidean speed calculation
- [ ] IOCTL READ/WRITE for configuration
- [ ] LUT population from userspace

### What Works Now
- Driver loads and creates `/dev/rawaccel`
- Automatically connects to all mice
- Buffers REL_X and REL_Y events until SYN_REPORT
- Calculates movement speed and applies LUT lookup
- Currently passes through events 1:1 (no acceleration yet)
- Ready for LUT configuration via IOCTL

### Next Steps
- Implement IOCTL READ/WRITE handlers
- Add LUT validation
- Backend: Implement LutComputer in C# (PLAN_BE_LUT_MIGRATION.md)

## Files

| File | Purpose |
|------|---------|
| `rawaccel_main.c` | Module entry point, device registration |
| `rawaccel_types.h` | Data structures (LUT, device context) |
| `rawaccel_lut.h` | LUT lookup with binary search + interpolation (header-only) |
| `rawaccel_ioctl.c` | IOCTL command handlers |
| `rawaccel_ioctl.h` | IOCTL interface definitions |
| `rawaccel_input.c` | Input handler for mouse event interception |
| `rawaccel_input.h` | Input handler interface |
| `test_ioctl.c` | Userspace test program |
| `Makefile` | Build configuration |
| `Kbuild` | Kernel build settings |
| `dkms.conf` | DKMS configuration |

## Troubleshooting

**Module won't load:**
- Check `dmesg | tail` for error messages
- Verify kernel headers are installed: `ls /lib/modules/$(uname -r)/build`

**Permission denied on /dev/rawaccel:**
- Device is created with 0666 permissions, should be accessible
- Check with: `ls -l /dev/rawaccel`

**Build errors:**
- Ensure you have the correct kernel headers
- Try `make clean` then `make` again
