project(
    'dinit-chimera',
    ['c', 'cpp'],
    version: '0.99.0',
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
dlibdir = get_option('libdir') / 'dinit'
tmpfdir = get_option('libdir') / 'tmpfiles.d'
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
    'done.sh',
    'fs-btrfs.sh',
    'fs-fsck.sh',
    'fs-fstab.sh',
    'fs-zfs.sh',
    'hostname.sh',
    'hwclock.sh',
    'local.sh',
    'lvm.sh',
    'machine-id.sh',
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
    'tmpfiles.sh',
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
    'init-machine-id',
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
    'init-tmpfiles',
    'init-tmpfiles-dev',
    'init-udev-settle',
    'init-udev-trigger',
    'init-udevd',
    'login.target',
    'network.target',
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
