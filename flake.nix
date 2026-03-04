{
  description = "wallpaper-runner (native Wayland MP4 wallpaper player)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = f: nixpkgs.lib.genAttrs systems (system:
        f system (import nixpkgs { inherit system; }));
    in
    {
      packages = forAllSystems (system: pkgs:
        let
          gstPlugins = with pkgs.gst_all_1; [
            gst-plugins-base
            gst-plugins-good
            gst-plugins-bad
            gst-libav
          ];
        in
        {
          default = pkgs.stdenv.mkDerivation {
            pname = "wallpaper-runner";
            version = "0.1.0";
            src = self;

            strictDeps = true;
            dontConfigure = true;

            nativeBuildInputs = [
              pkgs.zig
              pkgs.pkg-config
              pkgs.wrapGAppsHook4
            ];

            buildInputs = [
              pkgs.gtk4
              pkgs.gtk4-layer-shell
              pkgs.glib
              pkgs.gst_all_1.gstreamer
            ] ++ gstPlugins;

            buildPhase = ''
              runHook preBuild
              export HOME="$TMPDIR"
              zig build -Doptimize=ReleaseFast
              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall
              install -Dm755 zig-out/bin/wallpaper-runner $out/bin/wallpaper-runner
              runHook postInstall
            '';

            preFixup = ''
              gappsWrapperArgs+=(
                --prefix GST_PLUGIN_SYSTEM_PATH_1_0 : "${pkgs.lib.makeSearchPath "lib/gstreamer-1.0" gstPlugins}"
              )
            '';

            meta = with pkgs.lib; {
              description = "Native Wayland MP4 wallpaper player (gtk-layer-shell backend)";
              homepage = "https://github.com/rakhimgaliyev/wallpaper-runner";
              license = licenses.mit;
              platforms = platforms.linux;
              mainProgram = "wallpaper-runner";
            };
          };
        });

      apps = forAllSystems (system: pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/wallpaper-runner";
        };
      });

      devShells = forAllSystems (system: pkgs: {
        default = pkgs.mkShell {
          packages = [
            pkgs.zig
            pkgs.pkg-config
            pkgs.gtk4
            pkgs.gtk4-layer-shell
            pkgs.glib
            pkgs.gst_all_1.gstreamer
            pkgs.gst_all_1.gst-plugins-base
            pkgs.gst_all_1.gst-plugins-good
            pkgs.gst_all_1.gst-plugins-bad
            pkgs.gst_all_1.gst-libav
          ];
        };
      });
    };
}
