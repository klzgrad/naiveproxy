# Debug ARM Cortex-A9 static in QEMU

```
export EXTRA_FLAGS='target_cpu="arm" target_os="openwrt" arm_version=0 arm_cpu="cortex-a9" arm_float_abi="soft" arm_use_neon=false build_static=true no_madvise_syscall=true'
export OPENWRT_FLAGS='arch=arm_cortex-a9-static release=23.05.0 gcc_ver=12.3.0 target=bcm53xx subtarget=generic'
./get-clang.sh
./build.sh
```

See https://wiki.qemu.org/Documentation/Networking for example.

```
$ wget https://downloads.openwrt.org/releases/23.05.2/targets/armsr/armv7/openwrt-23.05.2-armsr-armv7-generic-initramfs-kernel.bin

$ qemu-system-arm -nographic -M virt -m 1024 -kernel openwrt-23.05.2-armsr-armv7-generic-initramfs-kernel.bin -device virtio-net,netdev=net0 -netdev user,id=net0,hostfwd=tcp::5555-:1080
...

root@OpenWrt:/# ip link del br-lan
root@OpenWrt:/# ip addr add 10.0.2.15/24 dev eth0
root@OpenWrt:/# ip route add default via 10.0.2.2
root@OpenWrt:/# nft flush ruleset
root@OpenWrt:/# echo nameserver 10.0.2.3 >/etc/resolv.conf
root@OpenWrt:/# scp user@10.0.2.2:/tmp/naive .
root@OpenWrt:/# ./naive --listen=socks://0.0.0.0:1080 --proxy=https://user:pass@example.com --log

user@host:/tmp$ curl -v --proxy socks5h://127.0.0.1:5555 example.com
```

Install GDB
```
root@OpenWrt:/# sed -i -e "s/https/http/" /etc/opkg/distfeeds.conf
root@OpenWrt:/# echo option http_proxy http://10.0.2.2:8080/ >>/etc/opkg.conf
root@OpenWrt:/# opkg update
root@OpenWrt:/# opkg install gdb
```

# Debug ARM64 static in QEMU

```
export EXTRA_FLAGS='target_cpu="arm64" target_os="openwrt" arm_cpu="cortex-a53" build_static=true no_madvise_syscall=true'
export OPENWRT_FLAGS='arch=aarch64_cortex-a53-static release=23.05.0 gcc_ver=12.3.0 target=sunxi subtarget=cortexa53'
./get-clang.sh
./build.sh
```

```
$ wget https://downloads.openwrt.org/releases/23.05.2/targets/armsr/armv8/openwrt-23.05.2-armsr-armv8-generic-initramfs-kernel.bin

$ qemu-system-aarch64 -m 1024 -M virt -cpu cortex-a53 -nographic -kernel openwrt-23.05.2-armsr-armv8-generic-initramfs-kernel.bin -device virtio-net,netdev=net0 -netdev user,id=net0,hostfwd=tcp::5555-:1080
...

root@OpenWrt:/# ip link del br-lan
root@OpenWrt:/# ip addr add 10.0.2.15/24 dev eth0
root@OpenWrt:/# ip route add default via 10.0.2.2
root@OpenWrt:/# nft flush ruleset
root@OpenWrt:/# echo nameserver 10.0.2.3 >/etc/resolv.conf
root@OpenWrt:/# scp user@10.0.2.2:/tmp/naive .
root@OpenWrt:/# ./naive --listen=socks://0.0.0.0:1080 --proxy=https://user:pass@example.com --log
user@host:/tmp$ curl -v --proxy socks5h://127.0.0.1:5555 example.com
```

# Debug MIPSEL static in QEMU

```
export EXTRA_FLAGS='target_cpu="mipsel" target_os="openwrt" mips_arch_variant="r2" mips_float_abi="soft" build_static=true no_madvise_syscall=true'
export OPENWRT_FLAGS='arch=mipsel_24kc-static release=23.05.0 gcc_ver=12.3.0 target=ramips subtarget=rt305x'
./get-clang.sh
./build.sh
```

```
$ wget https://downloads.openwrt.org/snapshots/targets/malta/le/lede-malta-le-vmlinux-initramfs.elf

$ qemu-system-mipsel -nographic -M malta -kernel lede-malta-le-vmlinux-initramfs.elf -m 64 -device pcnet,netdev=net0 -netdev user,id=net0,hostfwd=tcp::5555-:1080
...
(eth0 is set up by DHCP)

root@LEDE:/# iptables -F
(scp is too old)
user@host:/tmp$ nc -l -p 2222 <./naive
root@LEDE:/# nc 10.0.2.2 2222 >naive
^C
root@LEDE:/# chmod +x ./naive
user@host:/tmp$ nc -l -p2222 </etc/ssl/certs/ca-certificates.crt
root@LEDE:/# mkdir -p /etc/ssl/certs/
root@LEDE:/# nc 10.0.2.2 2222 >/etc/ssl/certs/ca-certificates.crt
^C
root@LEDE:/# ./naive --listen=socks://0.0.0.0:1080 --proxy=https://user:pass@example.com --log
user@host:/tmp$ curl -v --proxy socks5h://127.0.0.1:5555 example.com
```

## To exit QEMU in -nographic:

Press Ctrl-A
Press X
