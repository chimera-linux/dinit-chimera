if libudev_dep.found() and dinitctl_dep.found() and not get_option('libudev').disabled()
    udev_cargs = ['-DHAVE_UDEV']
    udev_deps = [dinitctl_dep, libudev_dep]
else
    udev_cargs = []
    udev_deps = []
endif

devsock = ['-DDEVMON_SOCKET="' + get_option('devmon-socket') + '"']

helpers = [
    ['binfmt',    ['binfmt.cc'], [], []],
    ['devclient', ['devclient.cc'], [], [devsock]],
    ['devmon',    ['devmon.cc'], udev_deps, udev_cargs + devsock],
    ['hwclock',   ['hwclock.cc'], [], []],
    ['swclock',   ['swclock.cc'], [], []],
    ['kmod',      ['kmod.cc'], [kmod_dep], []],
    ['lo',        ['lo.cc'], [], []],
    ['mnt',       ['mnt.cc'], [], []],
    ['seedrng',   ['seedrng.cc'], [], []],
    ['sysctl',    ['sysctl.cc'], [], []],
    ['swap',      ['swap.cc'], [], []],
]

foreach helper: helpers
    executable(
        helper[0], helper[1],
        dependencies: helper[2],
        cpp_args: helper[3],
        install: true,
        install_dir: earlydir / 'helpers'
    )
endforeach
