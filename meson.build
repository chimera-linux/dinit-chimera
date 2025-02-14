project(
    'dinit-chimera',
    ['cpp'],
    version: '0.99.18',
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
sbindir = get_option('sbindir')
dlibdir = get_option('libdir') / 'dinit'
tmpfdir = get_option('libdir') / 'tmpfiles.d'
srvdir = get_option('libdir') / 'dinit.d'
earlydir = srvdir / 'early'

cpp = meson.get_compiler('cpp')

kmod_dep = dependency('libkmod')
libudev_dep = dependency('libudev', required: get_option('libudev'))
dinitctl_dep = cpp.find_library('dinitctl', required: get_option('libudev'))

bless_boot_path = get_option('bless-boot-path')
dinit_console_path = get_option('dinit-console-path')
dinit_cryptdisks_path = get_option('dinit-cryptdisks-path')
dinit_devd_path = get_option('dinit-devd-path')
dinit_sulogin_path = get_option('dinit-sulogin-path')
dinit_path = pfx / sbindir / 'dinit'

if bless_boot_path == ''
    bless_boot_path = pfx / lexecdir / 'systemd-bless-boot'
endif

if dinit_console_path == ''
    dinit_console_path = pfx / lexecdir / 'dinit-console'
endif

if dinit_cryptdisks_path == ''
    dinit_cryptdisks_path = pfx / lexecdir / 'dinit-cryptdisks'
endif

if dinit_devd_path == ''
    dinit_devd_path = pfx / lexecdir / 'dinit-devd'
endif

if dinit_sulogin_path == ''
    dinit_sulogin_path = pfx / sbindir / 'sulogin'
endif

subdir('early/helpers')
subdir('early/scripts')
subdir('man')
subdir('services')
subdir('tmpfiles')
