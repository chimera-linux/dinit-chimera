svconfd = configuration_data()

svconfd.set('EARLY_PATH', pfx / srvdir / 'early')
svconfd.set('HELPER_PATH', pfx / srvdir / 'early/helpers')
svconfd.set('SCRIPT_PATH', pfx / srvdir / 'early/scripts')
svconfd.set('DINIT_SULOGIN_PATH', dinit_sulogin_path)

services = [
    'boot',
    'device',
    'early-binfmt',
    'early-bless-boot',
    'early-cgroups',
    'early-console.target',
    'early-cryptdisks',
    'early-cryptdisks-early',
    'early-devices.target',
    'early-devmon',
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
    'early-kernel-env',
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
        input: srv,
        output: srv,
        configuration: svconfd,
        format: 'cmake@',
        install: true,
        install_dir: srvdir,
        install_mode: 'rw-r--r--',
    )
endforeach
