devsock = ['-DDEVMON_SOCKET="' + get_option('devmon-socket') + '"']

helpers = [
    ['binfmt',    ['binfmt.cc'], [], []],
    ['devclient', ['devclient.cc'], [], [devsock]],
    ['hwclock',   ['hwclock.cc'], [], []],
    ['swclock',   ['swclock.cc'], [], []],
    ['kmod',      ['kmod.cc'], [kmod_dep], []],
    ['lo',        ['lo.cc'], [], []],
    ['mnt',       ['mnt.cc'], [], []],
    ['seedrng',   ['seedrng.cc'], [], []],
    ['sysctl',    ['sysctl.cc'], [], []],
    ['swap',      ['swap.cc'], [], []],
]

if build_machine.kernel() == 'linux'
    helpers += [['zram', ['zram.cc'], [], []]]
endif

if libudev_dep.found() and dinitctl_dep.found() and not get_option('libudev').disabled()
    helpers += [
        [
            'devmon',
            ['devmon.cc'],
            [dinitctl_dep, libudev_dep],
            ['-DHAVE_UDEV'] + devsock
        ]
    ]
endif

foreach helper: helpers
    executable(
        helper[0], helper[1],
        dependencies: helper[2],
        cpp_args: helper[3],
        install: true,
        install_dir: earlydir / 'helpers'
    )
endforeach

install_symlink('mnt-service', install_dir: earlydir / 'helpers', pointing_to: 'mnt')
