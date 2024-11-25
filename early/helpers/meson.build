helpers = [
    ['binfmt',  ['binfmt.cc'], []],
    ['hwclock', ['hwclock.cc'], []],
    ['swclock', ['swclock.cc'], []],
    ['kmod',    ['kmod.cc'], [kmod_dep]],
    ['lo',      ['lo.cc'], []],
    ['mnt',     ['mnt.cc'], []],
    ['seedrng', ['seedrng.cc'], []],
    ['sysctl',  ['sysctl.cc'], []],
    ['swap',    ['swap.cc'], []],
]

foreach helper: helpers
    executable(
        helper[0], helper[1],
        dependencies: helper[2],
        install: true,
        install_dir: earlydir / 'helpers'
    )
endforeach
