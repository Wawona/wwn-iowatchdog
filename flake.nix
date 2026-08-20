{
  description = "wwn-iowatchdog: macOS Watchdog tools for Wawona Desktop Mode B (soft-inject arm64e hook into watchdogd; IOWatchdog disable/enable over Unix socket). Never for iOS / App Store.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      darwinSystems = [ "aarch64-darwin" ];
      forAll = nixpkgs.lib.genAttrs darwinSystems;

      mkIowatchdog = pkgs: pkgs.stdenv.mkDerivation {
        pname = "wwn-iowatchdog";
        version = "0.2.0";
        src = ./.;
        # Darwin stdenv ships apple-sdk; do not use removed apple_sdk.frameworks.
        # Hook MUST be arm64e (watchdogd is arm64e). CLI/inject are host arm64.
        buildPhase = ''
          runHook preBuild
          mkdir -p build

          # arm64e hook dylib (loaded into /usr/libexec/watchdogd)
          $CC -O2 -Wall -Wextra -dynamiclib \
            -arch arm64e \
            -install_name /usr/local/lib/libwwn_watchdogd_hook.dylib \
            -o build/libwwn_watchdogd_hook.dylib \
            src/hook/wwn_watchdogd_hook.c \
            -framework IOKit -framework CoreFoundation

          # CLI + injector (host arch). Never link lldb.
          $CC -O2 -Wall -Wextra \
            -arch arm64e \
            -o build/wwn-iowatchdog \
            src/wwn-iowatchdog.c \
            src/inject/wwn_watchdogd_inject.c \
            -framework IOKit -framework CoreFoundation

          $CC -O2 -Wall -Wextra -DWWN_INJECT_MAIN \
            -arch arm64e \
            -o build/wwn-watchdogd-inject \
            src/inject/wwn_watchdogd_inject.c \
            -framework CoreFoundation

          file build/libwwn_watchdogd_hook.dylib build/wwn-iowatchdog
          runHook postBuild
        '';
        installPhase = ''
          runHook preInstall
          mkdir -p $out/bin $out/lib
          install -m755 build/wwn-iowatchdog $out/bin/wwn-iowatchdog
          install -m755 build/wwn-watchdogd-inject $out/bin/wwn-watchdogd-inject
          install -m755 build/libwwn_watchdogd_hook.dylib \
            $out/lib/libwwn_watchdogd_hook.dylib
          runHook postInstall
        '';
        meta = with pkgs.lib; {
          description = "Soft-inject arm64e hook into watchdogd; disable/re-enable IOWatchdog userspace monitoring (Desktop Mode B)";
          platforms = platforms.darwin;
          license = licenses.mit;
        };
      };
    in
    {
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

      lib = {
        mkPackage = { pkgs }: mkIowatchdog pkgs;
      };

      checks = forAll (system: {
        wwn-iowatchdog = self.packages.${system}.wwn-iowatchdog;
      });
    };
}
