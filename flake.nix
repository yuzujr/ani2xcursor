{
  description = "ani2xcursor — convert Windows animated cursors to Xcursor themes";
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs   = nixpkgs.legacyPackages.${system};
    in {

      # Build uses Makefile (not xmake) so no package manager runs in the sandbox.
      # All deps come from buildInputs via pkg-config / CPATH.
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname   = "ani2xcursor";
        version = "1.0.0";
        src     = ./.;

        nativeBuildInputs = with pkgs; [
          clang
          pkg-config
          gettext      # msgfmt for compiling translations
        ];
        buildInputs = with pkgs; [
          spdlog
          libxcursor
          libX11
        ];

        buildPhase = ''
          # stb is header-only with no .pc file expose via CPATH.
          export CPATH="${pkgs.stb}/include''${CPATH:+:$CPATH}"
          make -j$NIX_BUILD_CORES
        '';

        installPhase = ''
          runHook preInstall
          make install PREFIX=$out
          runHook postInstall
        '';

        meta = {
          description = "Convert Windows animated cursors (.ani/.cur) to Xcursor themes";
          homepage    = "https://github.com/yuzujr/ani2xcursor";
          license     = pkgs.lib.licenses.mit;
          platforms   = pkgs.lib.platforms.linux;
          mainProgram = "ani2xcursor";
        };
      };

      # Use xmake for development (network available, downloads pinned deps).
      # Pass --nix=y to use nix:: packages instead of downloading.
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          xmake
          pkg-config
          clang
          clang-tools
          gettext
          gdb
          spdlog
          libxcursor
          libX11
        ];
        shellHook = ''
          echo "ani2xcursor dev shell"
          echo "  xmake                build (downloads pinned deps)"
          echo "  xmake f --nix=y      build using nix:: packages"
          echo "  make                 build with plain make"
        '';
      };
    };
}
