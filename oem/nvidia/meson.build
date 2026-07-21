if get_option('oem-nvidia').enabled()
    sdeventplus_dep = dependency('sdeventplus')
    erot_deps = [
        sdeventplus_dep,
        sdbusplus_dep,
        phosphor_dbus_interfaces_dep,
        phosphor_logging_dep,
    ]

    source = [
        'main.cpp',
        'mctp_endpoint_discovery.cpp',
        'socket_handler.cpp',
        'erot_time_manager.cpp',
        'utils.cpp',
    ]

    executable(
        'erot-time-manager',
        source,
        include_directories: include_directories('..'),
        implicit_include_directories: false,
        dependencies: erot_deps,
        install: true,
        install_dir: get_option('bindir'),
    )

    configure_file(
        input: 'services/nvidia-erot-time-manager.service',
        output: 'nvidia-erot-time-manager.service',
        copy: true,
        install_dir: systemd_system_unit_dir,
    )
endif

if get_option('hmc-time-sync').enabled()
    curl_dep = dependency('libcurl')

    executable(
        'hmc-time-sync',
        'hmc_time_sync.cpp',
        include_directories: include_directories('..'),
        implicit_include_directories: false,
        dependencies: [curl_dep, phosphor_logging_dep],
        cpp_args: [
            '-DHMC_TIME_SYNC_HOST="' + get_option('hmc-time-sync-host') + '"',
        ],
        install: true,
        install_dir: get_option('libexecdir'),
    )

    configure_file(
        input: 'services/set-hmc-time.service',
        output: 'set-hmc-time.service',
        copy: true,
        install_dir: systemd_system_unit_dir,
    )

    configure_file(
        input: 'services/set-hmc-time.timer',
        output: 'set-hmc-time.timer',
        copy: true,
        install_dir: systemd_system_unit_dir,
    )
endif
