# dinit-chimera

This is the core services suite for [dinit](https://github.com/davmac314/dinit)
as used by Chimera.

It provides an expansive collection of service files, scripts and helpers to
aid early boot, more suitable for a practical deployment than the example
collection that comes with upstream. Patches for third party distro adaptations
are welcome, provided they are not disruptive.

Currently the documentation for the suite is lacking, which is also to be done.

## Dependencies

* [dinit](https://github.com/davmac314/dinit) (b74c5266fd2c7fbe64cdf7c93024bffe1f9365ff or newer)
* [chimerautils](https://github.com/chimera-linux/chimerautils) or GNU coreutils
* `awk` (POSIX will do)
* [kmod](https://git.kernel.org/pub/scm/utils/kernel/kmod/kmod.git)
* [util-linux](https://mirrors.edge.kernel.org/pub/linux/utils/util-linux)
  * Just portions of it (`fsck`, `findmnt`, `mount`, `mountpoint`, `swapon`)
* `sulogin` (any implementation)
* `systemd-udev` (`eudev` will work with some path changes)
* `systemd-tmpfiles` (for now, a builtin implementation is planned)

### Optional dependencies

Not having these dependencies will allow the boot to proceed, but specific
functionality will not work. Generally the affected oneshots will simply
exit with success if the tools aren't located.

* [procps](https://gitlab.com/procps-ng/procps)
  * For `sysctl` setup
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
services should not have to depend on `init-prepare.target` and so on.

* `init-prepare.target` - early pseudo-filesystems have been mounted
* `init-modules.target` - kernel modules from `/etc/modules` have been loaded
* `init-devices.target` - device events have been processed
  * This means `/dev` is fully populated with quirks applied and so on.
* `init-keyboard.target` - console keymap has been set
  * This has no effect when `setupcon` from `console-setup` is not available.
* `init-fs-pre.target` - filesystems are ready to be checked and mounted
  * This means encrypted disks, RAID, LVM and so on is up.
* `init-root-rw.target` - root filesystem has been re-mounted read/write.
  * That is, unless `fstab` explicitly specifies it should be read-only.
* `init-fs-fstab.target` - non-network filesystems in `fstab` have been mounted
* `init-fs-local.target` - non-network filesystems have finished mounting
  * This includes the above plus non-`fstab` filesystems such as ZFS.
* `init-console.target` - follow-up to `init-keyboard.target` (console font, etc.)
  * This has no effect when `setupcon` from `console-setup` is not available.
* `init-done.target` - most important early oneshots have fun.
  * Temporary/volatile files/dirs managed with `tmpfiles.d` are not guaranteed yet.
  * Most services should prefer `init-local.target` as their sentinel.
  * Typically only for services that should guarantee being up before `rc.local` is run.
  * All targets above this one are guaranteed to have been reached.
* `init-local.target` - `/etc/rc.local` has run and temp/volatile files/dirs are created
  * Implies `init-done.target`.
  * Most regular services should depend on at least this one (or `init-done.target`).
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
