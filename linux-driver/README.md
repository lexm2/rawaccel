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

### Phase 1: Core Infrastructure (In Progress)
- [x] Module skeleton (init/exit)
- [x] Makefile and Kbuild
- [x] DKMS configuration
- [x] Basic data structures (rawaccel_types.h)
- [x] misc device registration (/dev/rawaccel)
- [x] Basic IOCTL interface (GET_VERSION)
- [ ] input_handler registration
- [ ] Basic event interception
- [ ] Per-device context allocation

### Next Steps
- Implement input handler for mouse event interception
- Add LUT lookup implementation
- Add device enumeration and management

## Files

| File | Purpose |
|------|---------|
| `rawaccel_main.c` | Module entry point, device registration |
| `rawaccel_types.h` | Data structures (LUT, device context) |
| `rawaccel_ioctl.c` | IOCTL command handlers |
| `rawaccel_ioctl.h` | IOCTL interface definitions |
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
