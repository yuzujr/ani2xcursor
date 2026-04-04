{
  description = "ani2xcursor — convert Windows animated cursors to Xcursor themes";
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      lib = nixpkgs.lib;
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = lib.genAttrs systems;
      projectVersion = lib.removeSuffix "\n" (builtins.readFile ./VERSION);
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.stdenv.mkDerivation {
            pname = "ani2xcursor";
            version = projectVersion;
            src = ./.;

            nativeBuildInputs = with pkgs; [
              clang
              pkg-config
              gettext # msgfmt for compiling translations
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
              homepage = "https://github.com/yuzujr/ani2xcursor";
              license = lib.licenses.mit;
              platforms = lib.platforms.linux;
              mainProgram = "ani2xcursor";
            };
          };
        });

      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell.override { stdenv = pkgs.clangStdenv; } {
            packages = with pkgs; [
              xmake
              pkg-config
              clang-tools
              gettext
              gdb
              spdlog
              stb
              libxcursor
              libX11
            ];
          };
        });
    };
}
