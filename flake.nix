{
  description = "ani2xcursor — convert Windows animated cursors to Xcursor themes";
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs   = nixpkgs.legacyPackages.${system};
    in {

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
          stb
          libxcursor
          libX11
        ];

        buildPhase = ''
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

      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          xmake
          pkg-config
          clang
          clang-tools
          gettext
          gdb
          spdlog
          stb
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
