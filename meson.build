project(
    'dinit-chimera',
    ['cpp'],
    version: '0.99.12',
    default_options: [
        'prefix=/usr',
        'cpp_std=c++17',
        'cpp_eh=none', 'cpp_rtti=false',
        'warning_level=3',
        'buildtype=debugoptimized',
    ],
    license: 'BSD-2-Clause',
)

pfx = get_option('prefix')
lexecdir = get_option('libexecdir')
dlibdir = get_option('libdir') / 'dinit'
tmpfdir = get_option('libdir') / 'tmpfiles.d'
srvdir = get_option('libdir') / 'dinit.d'
earlydir = srvdir / 'early'

kmod_dep = dependency('libkmod')

bless_boot_path = get_option('bless-boot-path')
dinit_console_path = get_option('dinit-console-path')
dinit_devd_path = get_option('dinit-devd-path')

if bless_boot_path == ''
    bless_boot_path = pfx / lexecdir / 'systemd-bless-boot'
endif

if dinit_console_path == ''
    dinit_console_path = pfx / lexecdir / 'dinit-console'
endif

if dinit_devd_path == ''
    dinit_devd_path = pfx / lexecdir / 'dinit-devd'
endif

confd = configuration_data()

confd.set_quoted('BLESS_BOOT_PATH', bless_boot_path)
confd.set_quoted('DINIT_CONSOLE_PATH', dinit_console_path)
confd.set_quoted('DINIT_DEVD_PATH', dinit_devd_path)

confd.set('EARLY_PATH', pfx / srvdir / 'early')
confd.set('HELPER_PATH', pfx / srvdir / 'early/helpers')
confd.set('SCRIPT_PATH', pfx / srvdir / 'early/scripts')

svconfd = configuration_data()

svconfd.set('EARLY_PATH', pfx / srvdir / 'early')
svconfd.set('HELPER_PATH', pfx / srvdir / 'early/helpers')
svconfd.set('SCRIPT_PATH', pfx / srvdir / 'early/scripts')

helpers = [
    ['binfmt',  ['early/helpers/binfmt.cc'], []],
    ['hwclock', ['early/helpers/hwclock.cc'], []],
    ['swclock', ['early/helpers/swclock.cc'], []],
    ['kmod',    ['early/helpers/kmod.cc'], [kmod_dep]],
    ['lo',      ['early/helpers/lo.cc'], []],
    ['mntpt',   ['early/helpers/mntpt.cc'], []],
    ['seedrng', ['early/helpers/seedrng.cc'], []],
    ['sysctl',  ['early/helpers/sysctl.cc'], []],
    ['swap',    ['early/helpers/swap.cc'], []],
]

foreach helper: helpers
    executable(
        helper[0], helper[1],
        dependencies: helper[2],
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
    'dev.sh',
    'dmraid.sh',
    'done.sh',
    'env.sh',
    'fs-btrfs.sh',
    'fs-fsck.sh',
    'fs-fstab.sh',
    'fs-zfs.sh',
    'hostname.sh',
    'kdump.sh',
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
    'try-kdump.sh',
]

foreach scr: scripts
    configure_file(
        input: 'early/scripts/' + scr,
        output: scr,
        configuration: confd,
        format: 'cmake@',
        install: true,
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
    'early-kdump',
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
    'early-dev-settle',
    'early-dev-trigger',
    'early-devd',
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
    configure_file(
        input: 'services/' + srv,
        output: srv,
        configuration: svconfd,
        format: 'cmake@',
        install: true,
        install_dir: srvdir,
        install_mode: 'rw-r--r--',
    )
endforeach

# shutdown hook for oneshot actions
configure_file(
    input: 'shutdown-hook',
    output: 'shutdown-hook',
    configuration: confd,
    format: 'cmake@',
    install: true,
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
