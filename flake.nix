{
  description = "wwn-iowatchdog: macOS Watchdog tools for Wawona Desktop Mode B (IOWatchdog userspace monitoring, watchdogd safety). Never for iOS / App Store.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      # Host tools only. No Apple mobile / Android matrix.
      # Nixpkgs 26.11 throws on x86_64-darwin eval; flakehub-push --all-systems
      # needs a clean show, so omit Intel Darwin (same as other Wawona flakes).
      darwinSystems = [ "aarch64-darwin" ];
      forAll = nixpkgs.lib.genAttrs darwinSystems;

      mkIowatchdog = pkgs: pkgs.stdenv.mkDerivation {
        pname = "wwn-iowatchdog";
        version = "0.1.0";
        src = ./src;
        # Darwin stdenv ships apple-sdk; do not use removed apple_sdk.frameworks.
        buildPhase = ''
          runHook preBuild
          $CC -O2 -Wall -Wextra \
            -framework IOKit -framework CoreFoundation \
            -o wwn-iowatchdog wwn-iowatchdog.c
          runHook postBuild
        '';
        installPhase = ''
          runHook preInstall
          mkdir -p $out/bin
          install -m755 wwn-iowatchdog $out/bin/wwn-iowatchdog
          runHook postInstall
        '';
        meta = with pkgs.lib; {
          description = "Disable/re-enable macOS kernel IOWatchdog userspace monitoring (Desktop Mode B)";
          platforms = platforms.darwin;
          license = licenses.mit;
        };
      };
    in
    {
      # L3′ helper fragment. Not a graphics registry key; Wawona copies the
      # binary into Contents/Library/Wawona/ for desktop-host Mode B only.
      packages = forAll (system:
        let
          pkgs = import nixpkgs { inherit system; };
          iow = mkIowatchdog pkgs;
        in
        {
          wwn-iowatchdog = iow;
          default = iow;
        });

      apps = forAll (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.wwn-iowatchdog}/bin/wwn-iowatchdog";
        };
        wwn-iowatchdog = {
          type = "app";
          program = "${self.packages.${system}.wwn-iowatchdog}/bin/wwn-iowatchdog";
        };
      });

      # Future Watchdog tools land under src/ and get packages here.
      # Consumers: Wawona flake only (never L0-L2 flake inputs).
      lib = {
        # Convenience for callPackage / macos.nix: path to the bin derivation.
        mkPackage = { pkgs }: mkIowatchdog pkgs;
      };

      checks = forAll (system: {
        wwn-iowatchdog = self.packages.${system}.wwn-iowatchdog;
      });
    };
}
