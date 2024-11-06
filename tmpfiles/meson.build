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
        tmpf,
        install_dir: tmpfdir,
        install_mode: 'rw-r--r--',
    )
endforeach
