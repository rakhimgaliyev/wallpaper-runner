const std = @import("std");

extern fn run_wallpaper_player(video_path: [*:0]const u8, monitor_selector: [*:0]const u8, verbose: c_int) c_int;

const Version = "0.1.0";

const Options = struct {
    video: ?[]const u8 = null,
    monitor: []const u8 = "auto",
    verbose: bool = false,
    help: bool = false,
    version: bool = false,
};

const CliError = error{
    MissingValue,
    MissingVideo,
    UnknownArgument,
};

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const argv = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, argv);

    const options = parseArgs(argv) catch |err| {
        printCliError(err);
        printUsage();
        return std.process.exit(2);
    };

    if (options.help) {
        printUsage();
        return;
    }

    if (options.version) {
        std.debug.print("wallpaper-runner {s}\n", .{Version});
        return;
    }

    if (options.video == null) {
        printCliError(CliError.MissingVideo);
        printUsage();
        return std.process.exit(2);
    }

    const expanded_video = try expandUserPath(allocator, options.video.?);
    defer allocator.free(expanded_video);

    if (!fileExists(expanded_video)) {
        std.log.err("video file does not exist: {s}", .{expanded_video});
        return std.process.exit(1);
    }

    const video_z = try allocator.dupeZ(u8, expanded_video);
    defer allocator.free(video_z);

    const monitor_z = try allocator.dupeZ(u8, options.monitor);
    defer allocator.free(monitor_z);

    const rc = run_wallpaper_player(video_z.ptr, monitor_z.ptr, if (options.verbose) 1 else 0);
    if (rc != 0) {
        std.process.exit(@intCast(rc));
    }
}

fn parseArgs(argv: []const []const u8) CliError!Options {
    var options = Options{};

    var i: usize = 1;
    while (i < argv.len) {
        const arg = argv[i];

        if (std.mem.eql(u8, arg, "--help") or std.mem.eql(u8, arg, "-h")) {
            options.help = true;
            i += 1;
            continue;
        }

        if (std.mem.eql(u8, arg, "--version") or std.mem.eql(u8, arg, "-V")) {
            options.version = true;
            i += 1;
            continue;
        }

        if (std.mem.eql(u8, arg, "--verbose") or std.mem.eql(u8, arg, "-v")) {
            options.verbose = true;
            i += 1;
            continue;
        }

        if (std.mem.eql(u8, arg, "--video") or std.mem.eql(u8, arg, "-f")) {
            if (i + 1 >= argv.len) {
                return CliError.MissingValue;
            }
            options.video = argv[i + 1];
            i += 2;
            continue;
        }

        if (std.mem.eql(u8, arg, "--monitor") or std.mem.eql(u8, arg, "-m")) {
            if (i + 1 >= argv.len) {
                return CliError.MissingValue;
            }
            options.monitor = argv[i + 1];
            i += 2;
            continue;
        }

        if (std.mem.startsWith(u8, arg, "--")) {
            return CliError.UnknownArgument;
        }

        if (options.video == null) {
            options.video = arg;
            i += 1;
            continue;
        }

        return CliError.UnknownArgument;
    }

    return options;
}

fn expandUserPath(allocator: std.mem.Allocator, path: []const u8) ![]u8 {
    if (path.len == 0 or path[0] != '~') {
        return allocator.dupe(u8, path);
    }

    const home = std.process.getEnvVarOwned(allocator, "HOME") catch {
        return allocator.dupe(u8, path);
    };
    defer allocator.free(home);

    if (std.mem.eql(u8, path, "~")) {
        return allocator.dupe(u8, home);
    }

    if (path.len >= 2 and path[1] == '/') {
        return std.mem.concat(allocator, u8, &.{ home, path[1..] });
    }

    return allocator.dupe(u8, path);
}

fn fileExists(path: []const u8) bool {
    std.fs.cwd().access(path, .{}) catch return false;
    return true;
}

fn printCliError(err: CliError) void {
    switch (err) {
        CliError.MissingValue => std.log.err("missing value for flag", .{}),
        CliError.MissingVideo => std.log.err("missing --video <path>", .{}),
        CliError.UnknownArgument => std.log.err("unknown argument", .{}),
    }
}

fn printUsage() void {
    std.debug.print(
        \\Usage:
        \\  wallpaper-runner --video <path/to/file.mp4> [--monitor auto|DP-2|0] [--verbose]
        \\  wallpaper-runner <path/to/file.mp4>
        \\
        \\Options:
        \\  -f, --video <path>     MP4 file path.
        \\  -m, --monitor <value>  Monitor selector: auto, connector name (e.g. DP-2), or index (e.g. 0).
        \\  -v, --verbose           Enable verbose logs.
        \\  -V, --version           Print version.
        \\  -h, --help              Show this help.
        \\
        \\Notes:
        \\  - Linux/Wayland only. Uses gtk4-layer-shell as true background layer.
        \\  - Audio is always muted.
        \\  - Video loops forever.
        \\
    , .{});
}

test "parse args with flags" {
    const argv = [_][]const u8{ "wallpaper-runner", "--video", "/tmp/a.mp4", "--monitor", "DP-2", "--verbose" };
    const opts = try parseArgs(&argv);
    try std.testing.expect(opts.video != null);
    try std.testing.expectEqualStrings("/tmp/a.mp4", opts.video.?);
    try std.testing.expectEqualStrings("DP-2", opts.monitor);
    try std.testing.expect(opts.verbose);
}

test "parse positional video" {
    const argv = [_][]const u8{ "wallpaper-runner", "/tmp/a.mp4" };
    const opts = try parseArgs(&argv);
    try std.testing.expect(opts.video != null);
    try std.testing.expectEqualStrings("/tmp/a.mp4", opts.video.?);
    try std.testing.expectEqualStrings("auto", opts.monitor);
}

test "expand tilde" {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const out = try expandUserPath(allocator, "~/video.mp4");
    defer allocator.free(out);
    try std.testing.expect(out.len >= "/video.mp4".len);
}
