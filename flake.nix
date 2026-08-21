{
  description = "wwn-iowatchdog: macOS Watchdog tools for Wawona Desktop Mode B (Path A entitled open + claim; Path B arm64e hook sock). Never for iOS / App Store.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      darwinSystems = [ "aarch64-darwin" ];
      forAll = nixpkgs.lib.genAttrs darwinSystems;

      mkIowatchdog = pkgs: pkgs.stdenv.mkDerivation {
        pname = "wwn-iowatchdog";
        version = "0.3.5";
        src = ./.;
        # Darwin stdenv ships apple-sdk; do not use removed apple_sdk.frameworks.
        # Hook MUST be arm64e (watchdogd is arm64e). CLI/claim are host arm64.
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

          # CLI (host arch). Path A + Path B + claim helpers. Never link lldb.
          $CC -O2 -Wall -Wextra \
            -o build/wwn-iowatchdog \
            src/wwn-iowatchdog.c \
            src/common/wwn_watchdogd_job.c \
            src/direct/wwn_iowatchdog_direct.c \
            src/sock/wwn_iowatchdog_sock.c \
            src/inject/wwn_watchdogd_inject.c \
            -framework IOKit -framework CoreFoundation

          # Claim daemon (sticky disable + restore Apple's job)
          $CC -O2 -Wall -Wextra \
            -o build/wwn-iowatchdog-claim \
            src/claim/wwn-iowatchdog-claim.c \
            src/common/wwn_watchdogd_job.c \
            -framework IOKit -framework CoreFoundation

          # Unentitled claim-install (no private entitlements: interactive OK)
          $CC -O2 -Wall -Wextra \
            -o build/wwn-iowatchdog-claim-install \
            src/claim/wwn-iowatchdog-claim-install.c \
            src/common/wwn_watchdogd_job.c

          file build/libwwn_watchdogd_hook.dylib build/wwn-iowatchdog \
            build/wwn-iowatchdog-claim build/wwn-iowatchdog-claim-install
          runHook postBuild
        '';
        installPhase = ''
          runHook preInstall
          mkdir -p $out/bin $out/lib $out/share/wwn-iowatchdog
          install -m755 build/wwn-iowatchdog $out/bin/wwn-iowatchdog
          install -m755 build/wwn-iowatchdog-claim $out/bin/wwn-iowatchdog-claim
          install -m755 build/wwn-iowatchdog-claim-install \
            $out/bin/wwn-iowatchdog-claim-install
          install -m755 build/libwwn_watchdogd_hook.dylib \
            $out/lib/libwwn_watchdogd_hook.dylib
          install -m644 entitlements/wwn-iowatchdog.entitlements.plist \
            $out/share/wwn-iowatchdog/wwn-iowatchdog.entitlements.plist
          install -m755 scripts/claim-arm.sh $out/bin/wwn-iowatchdog-claim-arm
          install -m755 scripts/claim-disarm.sh \
            $out/bin/wwn-iowatchdog-claim-disarm
          runHook postInstall
        '';
        # Sign after strip. Host /usr/bin/codesign (sandbox PATH has none).
        # Ad-hoc forge of com.apple.private.iowatchdog.user-access: SIP-off
        # lab only. Apple will not grant this for Developer ID.
        # claim-install stays WITHOUT private entitlements (interactive arm).
        postFixup = ''
          ENT=$out/share/wwn-iowatchdog/wwn-iowatchdog.entitlements.plist
          /usr/bin/codesign --force -s - --entitlements "$ENT" \
            $out/bin/wwn-iowatchdog \
            $out/bin/wwn-iowatchdog-claim \
            $out/lib/libwwn_watchdogd_hook.dylib
          /usr/bin/codesign --force -s - \
            $out/bin/wwn-iowatchdog-claim-install
        '';
        meta = with pkgs.lib; {
          description = "IOWatchdog Mode B tools (Path A/B dual-path; fail-closed live inject on macOS 26)";
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
