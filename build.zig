const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "wallpaper-runner",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });

    exe.linkLibC();

    if (target.result.os.tag == .linux) {
        exe.addCSourceFile(.{
            .file = b.path("src/linux_player.c"),
            .flags = &.{"-std=c11"},
        });

        exe.linkSystemLibrary2("gtk4", .{ .use_pkg_config = .force });
        exe.linkSystemLibrary2("gtk4-layer-shell-0", .{ .use_pkg_config = .force });
        exe.linkSystemLibrary("dl");
    } else {
        exe.addCSourceFile(.{
            .file = b.path("src/stub_player.c"),
            .flags = &.{"-std=c11"},
        });
    }

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    const run_step = b.step("run", "Run wallpaper-runner");
    run_step.dependOn(&run_cmd.step);

    const unit_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    const run_unit_tests = b.addRunArtifact(unit_tests);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);
}
