
## Compile for iOS
 ```
 git clone --depth 1 https://github.com/klzgrad/naiveproxy.git
 cd naiveproxy/src
 ./get-clang.sh
 export EXTRA_FLAGS="target_cpu=\"arm64\" target_os=\"ios\" use_allocator=\"none\" use_allocator_shim=false enable_dsyms=false ios_enable_code_signing=false ios_deployment_target=\"12.0\""
 ./build.sh
 ```
