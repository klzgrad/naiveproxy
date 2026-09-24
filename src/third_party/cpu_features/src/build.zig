const std = @import("std");

pub fn build(b: *std.Build) void {
    // Standard target and optimization options
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Build options (mirroring CMake options)
    const build_executable = b.option(bool, "BUILD_EXECUTABLE", "Build list_cpu_features executable") orelse true;
    const enable_install = b.option(bool, "ENABLE_INSTALL", "Enable install targets") orelse true;

    const cpu_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    // Create the cpu_features static library
    const cpu_features = b.addLibrary(.{
        .name = "cpu_features",
        .linkage = .static,
        .root_module = cpu_mod,
    });

    cpu_mod.addIncludePath(b.path("include"));
    cpu_mod.addIncludePath(b.path("include/internal"));

    // Public compile definitions
    cpu_mod.addCMacro("STACK_LINE_READER_BUFFER_SIZE", "1024");

    // Platform-specific defines
    const os_tag = target.result.os.tag;
    const cpu_arch = target.result.cpu.arch;

    if (os_tag.isDarwin()) {
        cpu_mod.addCMacro("HAVE_SYSCTLBYNAME", "1");
    } else if (os_tag == .linux) {
        // Linux (including musl) provides getauxval() for hardware capability detection
        cpu_mod.addCMacro("HAVE_STRONG_GETAUXVAL", "1");
        cpu_mod.addCMacro("HAVE_DLFCN_H", "1");
    }

    // Utility sources (always included)
    const utility_sources = [_][]const u8{
        "src/filesystem.c",
        "src/stack_line_reader.c",
        "src/string_view.c",
    };

    // Common C flags for all source files
    const c_flags = [_][]const u8{
        "-Wall",
        "-Wextra",
        "-Wmissing-declarations",
        "-Wmissing-prototypes",
        "-Wno-implicit-fallthrough",
        "-Wno-unused-function",
        "-Wold-style-definition",
        "-Wshadow",
        "-Wsign-compare",
        "-Wstrict-prototypes",
        "-std=c99",
        "-fno-sanitize=undefined", // Disable UBSan for C code with intentional unaligned access
    };

    for (utility_sources) |source| {
        cpu_mod.addCSourceFile(.{
            .file = b.path(source),
            .flags = &c_flags,
        });
    }

    // Unix-based hardware detection (for non-x86 Unix platforms)
    // Note: Android is represented as Linux in Zig's target system
    if (os_tag != .windows and !cpu_arch.isX86()) {
        const hwcaps_sources = [_][]const u8{
            "src/hwcaps.c",
            "src/hwcaps_linux_or_android.c",
            "src/hwcaps_freebsd_or_openbsd.c",
        };

        for (hwcaps_sources) |source| {
            cpu_mod.addCSourceFile(.{
                .file = b.path(source),
                .flags = &c_flags,
            });
        }
    }

    // Architecture-specific implementation files
    // Determine which implementation files to include based on target architecture
    // Note: Android is represented as Linux in Zig's target system
    switch (cpu_arch) {
        .x86, .x86_64 => {
            // x86/x86_64 architecture
            const source: ?[]const u8 = if (os_tag == .linux)
                "src/impl_x86_linux_or_android.c"
            else if (os_tag.isDarwin())
                "src/impl_x86_macos.c"
            else if (os_tag == .windows)
                "src/impl_x86_windows.c"
            else if (os_tag == .freebsd)
                "src/impl_x86_freebsd.c"
            else
                null;

            if (source) |s| {
                cpu_mod.addCSourceFile(.{
                    .file = b.path(s),
                    .flags = &c_flags,
                });
            }

            cpu_features.installHeader(b.path("include/cpuinfo_x86.h"), "cpuinfo_x86.h");
        },
        .aarch64, .aarch64_be => {
            // AArch64 architecture - always needs cpuid
            cpu_mod.addCSourceFile(.{
                .file = b.path("src/impl_aarch64_cpuid.c"),
                .flags = &c_flags,
            });

            const source: ?[]const u8 = if (os_tag == .linux)
                "src/impl_aarch64_linux_or_android.c"
            else if (os_tag.isDarwin())
                "src/impl_aarch64_macos_or_iphone.c"
            else if (os_tag == .windows)
                "src/impl_aarch64_windows.c"
            else if (os_tag == .freebsd or os_tag == .openbsd)
                "src/impl_aarch64_freebsd_or_openbsd.c"
            else
                null;

            if (source) |s| {
                cpu_mod.addCSourceFile(.{
                    .file = b.path(s),
                    .flags = &c_flags,
                });
            }

            cpu_features.installHeader(b.path("include/cpuinfo_aarch64.h"), "cpuinfo_aarch64.h");
        },
        .arm, .armeb, .thumb, .thumbeb => {
            // ARM (32-bit) architecture
            if (os_tag == .linux) {
                cpu_mod.addCSourceFile(.{
                    .file = b.path("src/impl_arm_linux_or_android.c"),
                    .flags = &c_flags,
                });
            }
        },
        .mips, .mipsel, .mips64, .mips64el => {
            // MIPS architecture
            if (os_tag == .linux) {
                cpu_mod.addCSourceFile(.{
                    .file = b.path("src/impl_mips_linux_or_android.c"),
                    .flags = &c_flags,
                });
            }
            cpu_features.installHeader(b.path("include/cpuinfo_mips.h"), "cpuinfo_mips.h");
        },
        .powerpc, .powerpcle, .powerpc64, .powerpc64le => {
            // PowerPC architecture
            if (os_tag == .linux) {
                cpu_mod.addCSourceFile(.{
                    .file = b.path("src/impl_ppc_linux.c"),
                    .flags = &c_flags,
                });
            }
            cpu_features.installHeader(b.path("include/cpuinfo_ppc.h"), "cpuinfo_ppc.h");
        },
        .riscv32, .riscv64 => {
            // RISC-V architecture
            if (os_tag == .linux) {
                cpu_mod.addCSourceFile(.{
                    .file = b.path("src/impl_riscv_linux.c"),
                    .flags = &c_flags,
                });
            }
            cpu_features.installHeader(b.path("include/cpuinfo_riscv.h"), "cpuinfo_riscv.h");
        },
        .s390x => {
            // s390x architecture
            if (os_tag == .linux) {
                cpu_mod.addCSourceFile(.{
                    .file = b.path("src/impl_s390x_linux.c"),
                    .flags = &c_flags,
                });
            }
            cpu_features.installHeader(b.path("include/cpuinfo_s390x.h"), "cpuinfo_s390x.h");
        },
        .loongarch64 => {
            // LoongArch architecture
            if (os_tag == .linux) {
                cpu_mod.addCSourceFile(.{
                    .file = b.path("src/impl_loongarch_linux.c"),
                    .flags = &c_flags,
                });
            }
            cpu_features.installHeader(b.path("include/cpuinfo_loongarch.h"), "cpuinfo_loongarch.h");
        },
        else => {
            std.debug.print("Warning: Unsupported architecture {s}\n", .{@tagName(cpu_arch)});
        },
    }

    cpu_features.installHeader(b.path("include/cpu_features_cache_info.h"), "cpu_features_cache_info.h");
    cpu_features.installHeader(b.path("include/cpu_features_macros.h"), "cpu_features_macros.h");

    // Link against dl library on Unix-like systems
    if (os_tag != .windows and os_tag != .wasi) {
        cpu_mod.linkSystemLibrary("dl", .{});
    }

    // Install the library if enabled
    if (enable_install) {
        b.installArtifact(cpu_features);
    }

    const list_cpu_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    // Build list_cpu_features executable if requested
    if (build_executable) {
        const list_cpu_features = b.addExecutable(.{
            .name = "list_cpu_features",
            .root_module = list_cpu_mod,
        });

        list_cpu_mod.linkLibrary(cpu_features);

        list_cpu_mod.addCSourceFile(.{
            .file = b.path("src/utils/list_cpu_features.c"),
            .flags = &c_flags,
        });

        if (enable_install) {
            b.installArtifact(list_cpu_features);
        }

        // Add a run step for convenience
        const run_cmd = b.addRunArtifact(list_cpu_features);
        run_cmd.step.dependOn(b.getInstallStep());
        if (b.args) |args| {
            run_cmd.addArgs(args);
        }

        const run_step = b.step("run", "Run list_cpu_features");
        run_step.dependOn(&run_cmd.step);
    }
}
