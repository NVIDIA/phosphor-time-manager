deps = [
    sdeventplus,
    sdbusplus,
    phosphor_dbus_interfaces,
    logging
    ]

source = [
    'main.cpp', 
    'mctp_endpoint_discovery.cpp',
    'socket_handler.cpp',
    'erot_time_manager.cpp',
    'utils.cpp' ]

executable('erot-time-manager',source,
           implicit_include_directories: false,
           dependencies: deps,
           install: true,
           install_dir: get_option('bindir'))

configure_file(input: 'services/nvidia-erot-time-manager.service',
               output: 'nvidia-erot-time-manager.service',
               copy: true,
               install_dir: systemd_system_unit_dir)
