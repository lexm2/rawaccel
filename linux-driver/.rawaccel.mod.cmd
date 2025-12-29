savedcmd_rawaccel.mod := printf '%s\n'   rawaccel_main.o rawaccel_ioctl.o rawaccel_input.o | awk '!x[$$0]++ { print("./"$$0) }' > rawaccel.mod
