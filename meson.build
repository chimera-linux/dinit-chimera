project(
    'dinit-chimera',
    ['c', 'cpp'],
    version: '0.99.6',
    default_options: [
        'c_std=c99', 'cpp_std=c++17',
        'cpp_eh=none', 'cpp_rtti=false',
        'warning_level=3',
        'buildtype=debugoptimized',
    ],
    license: 'BSD-2-Clause',
)

dlibdir = get_option('libdir') / 'dinit'
tmpfdir = get_option('libdir') / 'tmpfiles.d'
srvdir = get_option('libdir') / 'dinit.d'
earlydir = srvdir / 'early'

helpers = [
    ['binfmt',  ['early/helpers/binfmt.cc']],
    ['hwclock', ['early/helpers/hwclock.c']],
    ['swclock', ['early/helpers/swclock.c']],
    ['lo',      ['early/helpers/lo.c']],
    ['mntpt',   ['early/helpers/mntpt.c']],
    ['seedrng', ['early/helpers/seedrng.c']],
    ['sysctl',  ['early/helpers/sysctl.cc']],
    ['swap',    ['early/helpers/swap.c']],
]

foreach helper: helpers
    executable(
        helper[0], helper[1],
        install: true,
        install_dir: earlydir / 'helpers'
    )
endforeach

manpages = [
    'early-modules.target.8'
]

foreach manp: manpages
    install_man('man/' + manp)
endforeach

scripts = [
    'binfmt.sh',
    'bless-boot.sh',
    'cgroups.sh',
    'clock.sh',
    'common.sh',
    'console.sh',
    'cryptdisks.sh',
    'dmraid.sh',
    'done.sh',
    'env.sh',
    'fs-btrfs.sh',
    'fs-fsck.sh',
    'fs-fstab.sh',
    'fs-zfs.sh',
    'hostname.sh',
    'local.sh',
    'lvm.sh',
    'machine-id.sh',
    'mdadm.sh',
    'modules-early.sh',
    'modules.sh',
    'net-lo.sh',
    'pseudofs.sh',
    'rng.sh',
    'root-fsck.sh',
    'root-remount.sh',
    'root-rw.sh',
    'swap.sh',
    'sysctl.sh',
    'tmpfs.sh',
    'tmpfiles.sh',
    'udev.sh',
]

foreach scr: scripts
    install_data(
        'early/scripts/' + scr,
        install_dir: earlydir / 'scripts',
        install_mode: 'rwxr-xr-x',
    )
endforeach

services = [
    'boot',
    'early-binfmt',
    'early-bless-boot',
    'early-cgroups',
    'early-console.target',
    'early-cryptdisks',
    'early-cryptdisks-early',
    'early-devices.target',
    'early-dmraid',
    'early-env',
    'early-fs-btrfs',
    'early-fs-fsck',
    'early-fs-fstab.target',
    'early-fs-local.target',
    'early-fs-pre.target',
    'early-fs-zfs',
    'early-hostname',
    'early-hwclock',
    'early-keyboard.target',
    'early-lvm',
    'early-machine-id',
    'early-mdadm',
    'early-modules-early',
    'early-modules',
    'early-modules.target',
    'early-net-lo',
    'early-prepare.target',
    'early-pseudofs',
    'early-rng',
    'early-root-fsck',
    'early-root-remount',
    'early-root-rw.target',
    'early-swap',
    'early-swclock',
    'early-sysctl',
    'early-tmpfs',
    'early-tmpfiles',
    'early-tmpfiles-dev',
    'early-udev-settle',
    'early-udev-trigger',
    'early-udevd',
    'local.target',
    'login.target',
    'network.target',
    'pre-local.target',
    'pre-network.target',
    'recovery',
    'single',
    'system',
    'time-sync.target',
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

tmpfiles = [
    'chimera.conf',
    'home.conf',
    'static-nodes-permissions.conf',
    'tmp.conf',
    'var.conf',
    'x11.conf',
]

foreach tmpf: tmpfiles
    install_data(
        'tmpfiles/' + tmpf,
        install_dir: tmpfdir,
        install_mode: 'rw-r--r--',
    )
endforeach
