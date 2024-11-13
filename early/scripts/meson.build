confd = configuration_data()

confd.set_quoted('DINIT_PATH', dinit_path)
confd.set_quoted('BLESS_BOOT_PATH', bless_boot_path)
confd.set_quoted('DINIT_CONSOLE_PATH', dinit_console_path)
confd.set_quoted('DINIT_CRYPTDISKS_PATH', dinit_cryptdisks_path)
confd.set_quoted('DINIT_DEVD_PATH', dinit_devd_path)

confd.set('EARLY_PATH', pfx / srvdir / 'early')
confd.set('HELPER_PATH', pfx / srvdir / 'early/helpers')
confd.set('SCRIPT_PATH', pfx / srvdir / 'early/scripts')

confd.set('DEFAULT_PATH_ENV', get_option('default-path-env'))

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
    'kernel-env.sh',
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
        input: scr,
        output: scr,
        configuration: confd,
        format: 'cmake@',
        install: true,
        install_dir: earlydir / 'scripts',
        install_mode: 'rwxr-xr-x',
    )
endforeach

# shutdown hook for oneshot actions
configure_file(
    input: '../../shutdown-hook',
    output: 'shutdown-hook',
    configuration: confd,
    format: 'cmake@',
    install: true,
    install_dir: dlibdir,
    install_mode: 'rwxr-xr-x',
)

# init file
configure_file(
    input: '../../init',
    output: 'init',
    configuration: confd,
    format: 'cmake@',
    install: true,
    install_dir: sbindir,
    install_mode: 'rwxr-xr-x',
)
