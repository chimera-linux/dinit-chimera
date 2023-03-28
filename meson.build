project(
    'dinit-chimera',
    ['c', 'cpp'],
    version: '0.1',
    default_options: [
        'c_std=c99', 'cpp_std=c++17',
        'cpp_eh=none', 'cpp_rtti=false',
        'warning_level=3',
        'buildtype=debugoptimized',
    ],
    license: 'BSD-2-Clause',
)

dexecdir = get_option('libexecdir') / 'dinit'
helperdir = dexecdir / 'helpers'
earlydir = dexecdir / 'early'
scsrvdir = get_option('sysconfdir') / 'dinit.d'
dlibdir = get_option('libdir') / 'dinit'
srvdir = get_option('libdir') / 'dinit.d'

helpers = [
    ['binfmt',  ['helpers/binfmt.cc']],
    ['hwclock', ['helpers/hwclock.c']],
    ['lo',      ['helpers/lo.c']],
    ['seedrng', ['helpers/seedrng.c']],
]

foreach helper: helpers
    executable(
        helper[0], helper[1],
        install: true,
        install_dir: helperdir
    )
endforeach

manpages = [
    'init-modules.target.8'
]

foreach manp: manpages
    install_man('man/' + manp)
endforeach

scripts = [
    'binfmt.sh',
    'cgroups.sh',
    'cryptdisks.sh',
    'dmraid.sh',
    'fs-btrfs.sh',
    'fs-fsck.sh',
    'fs-fstab.sh',
    'fs-zfs.sh',
    'hostname.sh',
    'hwclock.sh',
    'local.sh',
    'lvm.sh',
    'mdadm.sh',
    'modules-early.sh',
    'modules.sh',
    'pseudofs.sh',
    'rng.sh',
    'root-fsck.sh',
    'root-rw.sh',
    'setupcon.sh',
    'sysctl.sh',
    'tmpfs.sh',
]

foreach scr: scripts
    install_data(
        'scripts/' + scr,
        install_dir: earlydir,
        install_mode: 'rwxr-xr-x',
    )
endforeach

services = [
    'boot',
    'init-binfmt',
    'init-cgroups',
    'init-console.target',
    'init-cryptdisks',
    'init-cryptdisks-early',
    'init-devices.target',
    'init-dmraid',
    'init-done.target',
    'init-fs-btrfs',
    'init-fs-fsck',
    'init-fs-fstab.target',
    'init-fs-local.target',
    'init-fs-pre.target',
    'init-fs-zfs',
    'init-hostname',
    'init-hwclock',
    'init-keyboard.target',
    'init-local.target',
    'init-lvm',
    'init-mdadm',
    'init-modules-early',
    'init-modules.target',
    'init-net-lo',
    'init-prepare.target',
    'init-pseudofs',
    'init-rng',
    'init-root-fsck',
    'init-root-ro',
    'init-root-rw.target',
    'init-swap',
    'init-sysctl',
    'init-tmpfs',
    'init-udev-settle',
    'init-udev-trigger',
    'init-udevd',
    'login.target',
    'network.target',
    'pre-network.target',
    'recovery',
    'single',
    'system',
]

foreach srv: services
    install_data(
        'services/' + srv,
        install_dir: srvdir,
        install_mode: 'rw-r--r--',
    )
endforeach

# shutdown hook for oneshot actions
install_data(
    'shutdown-hook',
    install_dir: dlibdir,
    install_mode: 'rwxr-xr-x',
)

# keep boot.d dirs in place
install_data(
    'util/.empty',
    install_dir: srvdir / 'boot.d',
    install_mode: 'rw-r--r--',
)
install_data(
    'util/.empty',
    install_dir: scsrvdir / 'boot.d',
    install_mode: 'rw-r--r--',
)
