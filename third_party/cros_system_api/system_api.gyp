{
  'targets': [
    {
      'target_name': 'system_api-protos-gen',
      'type': 'none',
      'variables': {
        'proto_in_dir': 'dbus',
        'proto_out_dir': 'include/system_api/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/mtp_storage_info.proto',
        '<(proto_in_dir)/mtp_file_entry.proto',
        '<(proto_in_dir)/field_trial_list.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    {
      'target_name': 'system_api-protos',
      'type': 'static_library',
      'standalone_static_library': 1,
      'dependencies': [
        'system_api-protos-gen',
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/include/system_api/proto_bindings/mtp_storage_info.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/system_api/proto_bindings/mtp_file_entry.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/system_api/proto_bindings/field_trial_list.pb.cc',
      ]
    },
    {
      'target_name': 'system_api-power_manager-protos-gen',
      'type': 'none',
      'variables': {
        'proto_in_dir': 'dbus/power_manager',
        'proto_out_dir': 'include/power_manager/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/idle.proto',
        '<(proto_in_dir)/input_event.proto',
        '<(proto_in_dir)/peripheral_battery_status.proto',
        '<(proto_in_dir)/policy.proto',
        '<(proto_in_dir)/power_supply_properties.proto',
        '<(proto_in_dir)/suspend.proto',
        '<(proto_in_dir)/switch_states.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    {
      'target_name': 'system_api-power_manager-protos',
      'type': 'static_library',
      'standalone_static_library': 1,
      'dependencies': [
        'system_api-power_manager-protos-gen',
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/include/power_manager/proto_bindings/idle.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/power_manager/proto_bindings/input_event.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/power_manager/proto_bindings/peripheral_battery_status.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/power_manager/proto_bindings/policy.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/power_manager/proto_bindings/power_supply_properties.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/power_manager/proto_bindings/suspend.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/power_manager/proto_bindings/switch_states.pb.cc',
      ]
    },
    {
      'target_name': 'system_api-cryptohome-protos-gen',
      'type': 'none',
      'variables': {
        'proto_in_dir': 'dbus/cryptohome',
        'proto_out_dir': 'include/cryptohome/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/key.proto',
        '<(proto_in_dir)/rpc.proto',
        '<(proto_in_dir)/signed_secret.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    {
      'target_name': 'system_api-cryptohome-protos',
      'type': 'static_library',
      'standalone_static_library': 1,
      'dependencies': [
        'system_api-cryptohome-protos-gen',
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/include/cryptohome/proto_bindings/key.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/cryptohome/proto_bindings/rpc.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/cryptohome/proto_bindings/signed_secret.pb.cc',
      ]
    },
    {
      'target_name': 'system_api-authpolicy-protos-gen',
      'type': 'none',
      'variables': {
        'proto_in_dir': 'dbus/authpolicy',
        'proto_out_dir': 'include/authpolicy/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/active_directory_info.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    {
      'target_name': 'system_api-authpolicy-protos',
      'type': 'static_library',
      'standalone_static_library': 1,
      'dependencies': [
        'system_api-authpolicy-protos-gen',
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/include/authpolicy/proto_bindings/active_directory_info.pb.cc',
      ]
    },
    {
      'target_name': 'system_api-biod-protos-gen',
      'type': 'none',
      'variables': {
        'proto_in_dir': 'dbus/biod',
        'proto_out_dir': 'include/biod/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/constants.proto',
        '<(proto_in_dir)/messages.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    {
      'target_name': 'system_api-biod-protos',
      'type': 'static_library',
      'standalone_static_library': 1,
      'dependencies': [
        'system_api-biod-protos-gen',
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/include/biod/proto_bindings/constants.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/biod/proto_bindings/messages.pb.cc',
      ]
    },
    {
      'target_name': 'system_api-login_manager-protos-gen',
      'type': 'none',
      'variables': {
        'proto_in_dir': 'dbus/login_manager',
        'proto_out_dir': 'include/login_manager/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/arc.proto',
        '<(proto_in_dir)/policy_descriptor.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    {
      'target_name': 'system_api-login_manager-protos',
      'type': 'static_library',
      'standalone_static_library': 1,
      'dependencies': [
        'system_api-login_manager-protos-gen',
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/include/login_manager/proto_bindings/arc.pb.cc',
        '<(SHARED_INTERMEDIATE_DIR)/include/login_manager/proto_bindings/policy_descriptor.pb.cc',
      ]
    },
    {
      'target_name': 'system_api-chaps-protos-gen',
      'type': 'none',
      'variables': {
        'proto_in_dir': 'dbus/chaps',
        'proto_out_dir': 'include/chaps/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/ck_structs.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    {
      'target_name': 'system_api-chaps-protos',
      'type': 'static_library',
      # system_api-chaps-protos' is used by a shared_library
      # object, so we need to build it with '-fPIC' instead of '-fPIE'.
      'cflags!': ['-fPIE'],
      'cflags': ['-fPIC'],
      'standalone_static_library': 1,
      'dependencies': [
        'system_api-chaps-protos-gen',
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/include/chaps/proto_bindings/ck_structs.pb.cc',
      ]
    },
    {
      'target_name': 'system_api-smbprovider-protos-gen',
      'type': 'none',
      'variables': {
        'proto_in_dir': 'dbus/smbprovider',
        'proto_out_dir': 'include/smbprovider/proto_bindings',
      },
      'sources': [
        '<(proto_in_dir)/directory_entry.proto',
      ],
      'includes': ['../../platform2/common-mk/protoc.gypi'],
    },
    {
      'target_name': 'system_api-smbprovider-protos',
      'type': 'static_library',
      'standalone_static_library': 1,
      'dependencies': [
        'system_api-smbprovider-protos-gen',
      ],
      'sources': [
        '<(SHARED_INTERMEDIATE_DIR)/include/smbprovider/proto_bindings/directory_entry.pb.cc',
      ]
    },
  ]
}
