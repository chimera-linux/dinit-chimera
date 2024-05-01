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
* Basic core utilities
  * [chimerautils](https://github.com/chimera-linux/chimerautils) is most tested
  * GNU coreutils, busybox etc. may work (patches welcome)
* POSIX shell
* `awk` (POSIX will do)
* `modprobe`
  * Must have blacklist support
* `mount`, `umount`
  * Implementation must support `-a`
* `sulogin` (any implementation)
* `systemd-udev` (`eudev` will work with some path changes)
* `systemd-tmpfiles` (for now, a builtin implementation is planned)

### Optional dependencies

Not having these dependencies will allow the boot to proceed, but specific
functionality will not work. Generally the affected oneshots will simply
exit with success if the tools aren't located.

* `fsck`
  * Without it, early file system checks won't be available
  * Tested with `util-linux`, others may work
* [console-setup](https://salsa.debian.org/installer-team/console-setup)
  * For console keymap, font and so on.
* [mdadm](https://git.kernel.org/pub/scm/utils/mdadm/mdadm.git)
* [dmraid](https://people.redhat.com/~heinzm/sw/dmraid)
* [LVM2](https://sourceware.org/lvm2)
* [Btrfs](https://btrfs.readthedocs.io/en/latest)
* [ZFS](https://openzfs.github.io/openzfs-docs)

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
  * This has no effect when `setupcon` from `console-setup` is not available.
* `early-fs-pre.target` - filesystems are ready to be checked and mounted
  * This means encrypted disks, RAID, LVM and so on is up.
* `early-root-rw.target` - root filesystem has been re-mounted read/write.
  * That is, unless `fstab` explicitly specifies it should be read-only.
* `early-fs-fstab.target` - non-network filesystems in `fstab` have been mounted
* `early-fs-local.target` - non-network filesystems have finished mounting
  * This includes the above plus non-`fstab` filesystems such as ZFS.
* `early-console.target` - follow-up to `early-keyboard.target` (console font, etc.)
  * This has no effect when `setupcon` from `console-setup` is not available.
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
