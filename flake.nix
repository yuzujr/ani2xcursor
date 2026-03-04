{
  description = "ani2xcursor — convert Windows animated cursors to Xcursor themes";

  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs   = nixpkgs.legacyPackages.${system};
    in {

      # \u2500\u2500 Package \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname   = "ani2xcursor";
        version = "1.0.0";
        src     = ./.;

        nativeBuildInputs = with pkgs; [
          xmake
          pkg-config
          gettext        # provides msgfmt for translation compilation
          clang
        ];

        # spdlog and libxcursor have .pc files \u2014 xmake --system=y finds them
        # via PKG_CONFIG_PATH which stdenv sets up automatically.
        buildInputs = with pkgs; [
          spdlog
          libxcursor
          libX11
        ];

        buildPhase = ''
          export HOME=$TMPDIR
          export XMAKE_GLOBALDIR=$TMPDIR/.xmake

          # stb is header-only (no .pc file); add its include dir via CPATH.
          export CPATH="${pkgs.stb}/include''${CPATH:+:$CPATH}"

          # --system=y: use pkgconfig:: for spdlog/libxcursor, stb via CPATH.
          xmake f --yes -p linux -a x86_64 --mode=release --system=y
          xmake build --yes
        '';

        installPhase = ''
          runHook preInstall
          xmake install --yes -o $out
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

      # \u2500\u2500 Dev shell \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
      # Usage:
      #   nix develop
      #   xmake f --nix=y   # all deps from nix:: (nix daemon available here)
      #   xmake
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
          echo "  xmake f --nix=y   \u2014 configure (all deps from nix::)"
          echo "  xmake             \u2014 build"
          echo "  xmake run         \u2014 build & run"
          echo "  xmake test        \u2014 run tests"
        '';
      };
    };
}

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs   = nixpkgs.legacyPackages.${system};

      # ── Fixed-output derivation: pre-fetch xmake packages ────────
      # (spdlog, stb, libxcursor are fetched by xmake's own package manager.)
      # Run `nix build .#xmakeDeps` once — it fails and prints the
      # correct sha256; paste it into outputHash below.
      xmakeDeps = pkgs.stdenv.mkDerivation {
        name = "ani2xcursor-xmake-deps";
        src  = ./.;

        nativeBuildInputs = with pkgs; [
          xmake curl git cacert pkg-config
          libxcursor libX11   # pkg-config hints for libxcursor build
        ];

        outputHashAlgo = "sha256";
        outputHashMode = "recursive";
        # Fill in the real hash after the first `nix build` attempt:
        #   nix build .#xmakeDeps 2>&1 | grep "got:"
        outputHash     = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

        buildPhase = ''
          export HOME=$TMPDIR
          export XMAKE_GLOBALDIR=$TMPDIR/.xmake
          xmake f --yes -p linux -a x86_64 2>&1
        '';

        installPhase = ''
          cp -r $TMPDIR/.xmake $out
        '';
      };

    in {

      # ── Package ──────────────────────────────────────────────────
      packages.${system} = {
        xmakeDeps = xmakeDeps;

        default = pkgs.stdenv.mkDerivation {
          pname   = "ani2xcursor";
          version = "1.0.0";
          src     = ./.;

          nativeBuildInputs = with pkgs; [
            xmake pkg-config gettext   # gettext provides msgfmt
          ];
          buildInputs = with pkgs; [
            libxcursor libX11
          ];

          buildPhase = ''
            export HOME=$TMPDIR
            export XMAKE_GLOBALDIR=$TMPDIR/.xmake
            cp -rT ${xmakeDeps} $TMPDIR/.xmake
            chmod -R +w $TMPDIR/.xmake

            xmake f --yes -p linux -a x86_64 --network=none
            xmake build --yes
          '';

          installPhase = ''
            runHook preInstall
            xmake install --yes -o $out
            # Locales are compiled by xmake's after_install hook; ensure dir
            install -d $out/bin
            runHook postInstall
          '';

          meta = {
            description = "Convert Windows animated cursors (.ani/.cur) to Xcursor themes";
            homepage    = "https://github.com/yuzujr/ani2xcursor";
            license     = pkgs.lib.licenses.mit;
            platforms   = pkgs.lib.platforms.linux;
          };
        };
      };

      # ── Dev shell ────────────────────────────────────────────────
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          xmake
          pkg-config
          clang
          clang-tools    # clangd, clang-format
          gettext        # msgfmt for translations
          gdb
          # These satisfy pkg-config lookups that libxcursor needs
          libxcursor
          libX11
        ];

        shellHook = ''
          echo "ani2xcursor dev shell"
          echo "  xmake          — build"
          echo "  xmake run      — build & run (from project dir)"
          echo "  xmake test     — run tests"
        '';
      };
    };
}
