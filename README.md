# dinit-chimera

This is the core services suite for [dinit](https://github.com/davmac314/dinit)
as used by Chimera.

It provides an expansive collection of service files, scripts and helpers to
aid early boot, more suitable for a practical deployment than the example
collection that comes with upstream. Patches for third party distro adaptations
are welcome, provided they are not disruptive.

Currently the documentation for the suite is lacking, which is also to be done.

## Dependencies

* [dinit](https://github.com/davmac314/dinit) (0.18.0 or newer)
* Linux kernel 5.10 or newer
* POSIX shell
* POSIX core utilities
  * We test [chimerautils](https://github.com/chimera-linux/chimerautils)
  * Others are supported (GNU, `busybox`, etc.); issues should be reported
* `mount`, `umount`
  * Implementation must support `-a`
* `sulogin` (any implementation, e.g. `shadow`, `util-linux`, `busybox`)
* [sd-tools](https://github.com/chimera-linux/sd-tools) (particularly `sd-tmpfiles`)
* [libkmod](https://github.com/kmod-project/kmod)
* [libdinitctl](https://github.com/chimera-linux/libdinitctl)
* `libudev` (necessary for device dependencies; optional)

### Distribution-provided files

The distribution should provide the following helpers (the paths are the
defaults, they may be altered with meson options):

* `/usr/libexec/dinit-console`
  * Perform console and keyboard setup; optional
* `/usr/libexec/dinit-cryptdisks`
  * Perform encrypted drive setup; optional
* `/usr/libexec/dinit-devd`
  * Perform device initialization; mandatory

The `dinit-console` may look like this when using `console-setup`:

```
#!/bin/sh

if [ "$1" = "keyboard" ]; then
    set -- "-k"
else
    set --
fi

exec setupcon "$@"
```

The `dinit-cryptdisks` may look like this when using Debian `cryptsetup` scripts:

```
#!/bin/sh

[ -r /usr/lib/cryptsetup/cryptdisks-functions ] || exit 0
[ -r /etc/crypttab ] || exit 0

. /usr/lib/cryptsetup/cryptdisks-functions

INITSTATE="$1"

case "$2" in
    start) do_start ;;
    stop) do_stop ;;
    *) exit 1 ;;
esac
```

It is passed two arguments, the first one is either `early` or `remaining`
while the second one is either `start` or `stop`.

The `dinit-devd` may look like this when using `udev`:

```
#!/bin/sh

case "$1" in
    start) exec /usr/libexec/udevd --daemon ;;
    stop) udevadm control -e || : ;;
    settle) exec udevadm settle ;;
    trigger) exec udevadm trigger --action=add ;;
esac

exit 1
```

Note that currently the behaviors are subject to change. Adopters should
watch out for such changes and adjust their scripts accordingly.

### Optional dependencies

Not having these dependencies will allow the boot to proceed, but specific
functionality will not work. Generally the affected oneshots will simply
exit with success if the tools aren't located.

* `fsck`
  * Without it, early file system checks won't be available
  * Tested with `util-linux`, others may work
* [mdadm](https://git.kernel.org/pub/scm/utils/mdadm/mdadm.git)
* [dmraid](https://people.redhat.com/~heinzm/sw/dmraid)
* [LVM2](https://sourceware.org/lvm2)
* [Btrfs](https://btrfs.readthedocs.io/en/latest)
* [ZFS](https://openzfs.github.io/openzfs-docs)
* [makedumpfile](https://github.com/makedumpfile/makedumpfile)
  * For kernel crashdump support
* [kexec-tools](https://kernel.org/pub/linux/utils/kernel/kexec)
  * For kernel crashdump support

## Kernel command line

This suite implements a variety of kernel command line parameters that
you can use for debugging and other purposes.

### Dinit arguments

* `dinit_auto_recovery=1` - passes `--auto-recovery`
* `dinit_quiet=1` - passes `--quiet`
* `dinit_log_file=LOGFILE` - passes `--log-file LOGFILE`
* `dinit_log_level=LOGLEVEL` - passes `--log-level LOGLEVEL`
* `dinit_console_level=LOGLEVEL` - passes `--console-level LOGLEVEL`

These are notably useful for early boot debugging. There are a lot of
early services, and if a very early service fails, the real error very
quickly scrolls past the standard verbose output as services get stopped.
Previously this required unreliable workarounds like slow-motion screen
recording; now you can edit your kernel command line and add something
like `dinit_quiet=1 dinit_console_level=warn` to supress the "started"
and "stopped" messages.

These are all unset so they will not make it into the activation environment.

Additionally, there are more parameters that are purely for the purpose
of boot debugging and are implemented by `dinit-chimera` itself:

* `dinit_early_debug=1` - enables early debugging, causing each early
  service to echo a message before it performs its action; the following
  parameters only take effect if this is set
* `dinit_early_debug_slow=N` - sleeps `N` seconds after the echo and before
  performing the action, intentionally slowing down the boot process for
  better clarity
* `dinit_early_debug_log=LOGFILE` - instead of the console, all output will
  be redirected to the `LOGFILE`; note that you have to ensure the location
  of the file is writable

The debug parameters are subject to change if necessary. They become a part
of the global activation environment.

### Fsck arguments

* `fastboot` or `fsck.mode=skip` - skips filesystem checks
* `forcefsck` or `fsck.mode=force` - passes `-f` to `fsck`
* `fsckfix` or `fsck.repair=yes` - passes `-y` to `fsck` (do not ask questions)
* `fsck.repair=no` - passes `-n` to `fsck`

### Kdump arguments

These only apply if the optional kdump service is installed.

* `nokdump` - do not save kernel dump even if `/proc/vmcore` exists

### Tmpfs arguments

* `dinit.runsize=N` or `initramfs.runsize=N` - the `size=` parameter to
  use when mounting `/run` and `/run/user`; they are equivalent and the
  former is specific to `dinit`, while the latter exists for compatibility
  with `initramfs-tools` (as the initramfs will mount `/run` already and
  then `dinit-chimera` will not). Defaults to `10%`.

### Mount arguments

* `dinit_early_root_remount=VAL` the extra `remount` parameters to use for
  early root remount; the default is `ro,rshared` - this can be used to prevent
  read-only remount of the root filesystem, e.g. for debugging. Note that this
  variable makes it into the global activation environment.
* `dinit_skip_volumes` skip ZFS pools, LVM, as well as btrfs scan on early
  boot; particularly useful for e.g. live images, where doing this automatically
  is counterproductive and may even break things (e.g. for root ZFS pools).

## Device dependencies

The `dinit-chimera` suite allows services to depend on devices. Currently,
it is possible to depend on individual devices (`/dev/foo`), on `/sys` paths,
on network interfaces, on MAC addresses, and on USB `vendor:product` strings;
this is set by the argument provided to the `device` service.

For devices, it just looks like `/dev/foo`, for `/sys` paths it's a long native
path like `/sys/devices/...`, for network interfaces it's `netif:foo`, for MAC
addresses it's `mac:foo` (the address must be in lowercase format), for USB
IDs it's `usb:vendor:product` with lowercase hex (e.g. `usb:1d6b:0003`).
Additionally, disk aliases are supported, e.g. `device@PARTLABEL=foo` is equal
to `device@/dev/disk/by-partlabel/foo`.

For non-USB devices, they may appear and disappear according to their syspath.
For USB devices, which cannot be matched accurately by a syspath as you may have
multiple devices with the same vendor/product ID pair in your system, they
appear with the first device and disappear with the last device.

Devices from the `block`, `net`, `tty`, and `usb` subsystems are matched
automatically.
If you wish to match devices from other subsystems, they have to carry
the tag `dinit` or `systemd` (for compatibility).

For this functionality to work, it is necessary to build the suite with
`libudev` support; all device dependencies will fail when this is not done.

Example service that will not come up unless `/dev/sda1` is around, and will
shut down if `/dev/sda1` disappears:

```
type = process
command = /usr/bin/foo
depends-on: local.target
depends-on: device@/dev/sda1
```

This one will wait for a particular wireless interface but will not shut down
if it happens to disappear:

```
type = process
command = /usr/bin/foo
depends-on: local.target
depends-ms: device@netif:wlp170s0
```

It is also possible to create soft dependencies of the device services on
other services from within `udev` rules. To do this, the `DINIT_WAITS_FOR`
property can be used and the `dinit` tag must exist on the device. Like so:

```
TAG+="dinit", ENV{DINIT_WAITS_FOR}+="svc1 svc2"
```

Any service that depends on a device service belonging to the above will
be held back until the specified services have started or failed to start.

## Zram support

This suite supports management of zram devices on Linux.

The following configuration files are checked:

```
/etc/dinit-zram.d/*.conf
/run/dinit-zram.d/*.conf
/usr/local/lib/dinit-zram.d/*.conf
/usr/lib/dinit-zram.d/*.conf
/etc/dinit-zram.conf
```

The directory snippet paths are checked in that order and the first directory
to contain a config snippet of that name is prioritized (i.e. every file name
is only loaded once). The `/etc/dinit-zram.conf` configuration file is loaded
last and always (if it exists).

The syntax is like this:

```
; a comment
# also a comment
[zram0]
size = 4G
algorithm = zstd
format = mkswap -U clear %0
```

Fields that are specified later override those that are specified earlier,
so you can have e.g. a config file defining a zram device and then a later
one defining more details for it.

The above fields are currently the only supported ones (more will be added
later as well as more syntax). All but `size` are optional. The `format`
field specifies a command to use to format the device once set up and the
default is the one above, to set up swap space. You can set custom commands
for e.g. zram ramdisks with real filesystems on them.

Once you have a configuration file, you can activate the device by enabling
the `zram-device@zramN` service.

## Mount services

This suite supports mount services, which are service-driven supervised
mounts. You can define a mount service like this:

```
# /etc/dinit.d/usb-stick.mount
type = process
command = $DINIT_MOUNT \
    --from PARTLABEL=usbstick \
    --to /media/usb \
    --type ext4
restart = false
depends-on: device@PARTLABEL=usbstick
depends-on: early-fs-local.target
```

Starting this service will ensure that `/dev/sda1` will remain mounted for
as long as the device exists. Stopping the service will cleanly unmount
it. The `restart = false` ensures manually unmounting the device will not
remount it; `restart = true` will make sure it's always mounted, unless
stopped explicitly.

## Service targets

The collection provides special "target" services, suffixed with `.target`,
which can be used as dependencies for third party service files as well as
for ordering.

Until better documentation is in place, here is the list, roughly in bootup
order. The actual order may vary somewhat because of parallel startup. In
general your services should specify dependency links and ordering links
for every target that is relevant to your functionality (i.e. you should
not rely on transitive dependencies excessively). This does not apply
to very early oneshots that are guaranteed to have run, i.e. in most cases
services should not have to depend on `early-prepare.target` and so on.

* `early-prepare.target` - early pseudo-filesystems have been mounted
* `early-modules.target` - kernel modules from `/etc/modules` have been loaded
* `early-devices.target` - device events have been processed
  * This means `/dev` is fully populated with quirks applied and so on.
* `early-keyboard.target` - console keymap has been set
* `early-fs-pre.target` - filesystems are ready to be checked and mounted
  * This means encrypted disks, RAID, LVM and so on is up.
* `early-root-rw.target` - root filesystem has been re-mounted read/write.
  * That is, unless `fstab` explicitly specifies it should be read-only.
* `early-fs-fstab.target` - non-network filesystems in `fstab` have been mounted
* `early-fs-local.target` - non-network filesystems have finished mounting
  * This includes the above plus non-`fstab` filesystems such as ZFS.
* `early-console.target` - follow-up to `early-keyboard.target` (console font, etc.)
* `pre-local.target` - most important early oneshots have run.
  * Temporary/volatile files/dirs managed with `tmpfiles.d` are not guaranteed yet.
  * Most services should prefer `local.target` as their sentinel.
  * Typically only for services that should guarantee being up before `rc.local` is run.
  * All targets above this one are guaranteed to have been reached.
* `local.target` - `/etc/rc.local` has run and temp/volatile files/dirs are created
  * Implies `pre-local.target`.
  * Most regular services should depend on at least this one (or `pre-local.target`).
* `pre-network.target` - networking daemons may start.
  * This means things such as firewall have been brought up.
* `network.target` - networking daemons have started.
  * Networking daemons should use this as `before`.
  * Things depending on network being up should use this as a dependency.
* `login.target` - the system is ready to run gettys, launch display manager, etc.
  * Typically to be used as a `before` sentinel for things that must be up before login.
* `time-sync.target` - system date/time should be set by now.
  * Things such as NTP implementations should wait and use this as `before`.
  * Things requiring date/time to be set should use this as a dependency.
  * This may take a while, so pre-login services depending on this may stall the boot.
