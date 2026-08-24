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
        version = "0.3.15";
        src = ./.;
        # Darwin stdenv ships apple-sdk; do not use removed apple_sdk.frameworks.
        # Hook MUST be arm64e (watchdogd is arm64e). CLI/claim are host arm64.
        buildPhase = ''
          runHook preBuild
          mkdir -p build

          # Fail closed: never emit launchctl kickstart -k as a command.
          if grep -RInE 'launchctl[[:space:]]+kickstart[[:space:]]+-k' src; then
            echo "FORBIDDEN: launchctl kickstart -k in src" >&2
            exit 1
          fi
          if grep -RInE 'system\("[^"]*kickstart -k' src; then
            echo "FORBIDDEN: system(kickstart -k) in src" >&2
            exit 1
          fi

          # arm64e hook dylib (DYLD_INTERPOSE; no fishhook GOT patch)
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
            src/common/wwn_safety.c \
            src/direct/wwn_iowatchdog_direct.c \
            src/sock/wwn_iowatchdog_sock.c \
            src/inject/wwn_watchdogd_inject.c \
            -framework IOKit -framework CoreFoundation

          # Claim daemon (sticky disable + restore Apple's job)
          $CC -O2 -Wall -Wextra \
            -o build/wwn-iowatchdog-claim \
            src/claim/wwn-iowatchdog-claim.c \
            src/common/wwn_watchdogd_job.c \
            src/common/wwn_safety.c \
            src/sock/wwn_iowatchdog_sock.c \
            -framework IOKit -framework CoreFoundation

          # Unentitled claim-install (no private entitlements: interactive OK)
          $CC -O2 -Wall -Wextra \
            -o build/wwn-iowatchdog-claim-install \
            src/claim/wwn-iowatchdog-claim-install.c \
            src/common/wwn_watchdogd_job.c \
            src/common/wwn_safety.c \
            src/sock/wwn_iowatchdog_sock.c

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
          install -m644 entitlements/wwn-iowatchdog-claim.entitlements.plist \
            $out/share/wwn-iowatchdog/wwn-iowatchdog-claim.entitlements.plist
          install -m755 scripts/claim-arm.sh $out/bin/wwn-iowatchdog-claim-arm
          install -m755 scripts/claim-disarm.sh \
            $out/bin/wwn-iowatchdog-claim-disarm
          install -m755 scripts/patha-amfi-nvram.sh \
            $out/bin/wwn-iowatchdog-patha-amfi-nvram
          runHook postInstall
        '';
        # Sign after strip. Host /usr/bin/codesign (sandbox PATH has none).
        # CLI keeps full entitlements (task_for_pid for status).
        # Claim gets ONLY iowatchdog.user-access (leaner AMFI surface).
        # Hook: ad-hoc, no private entitlements (runs inside Apple's binary).
        # claim-install stays WITHOUT private entitlements (interactive arm).
        postFixup = ''
          ENT=$out/share/wwn-iowatchdog/wwn-iowatchdog.entitlements.plist
          ENT_CLAIM=$out/share/wwn-iowatchdog/wwn-iowatchdog-claim.entitlements.plist
          /usr/bin/codesign --force -s - --entitlements "$ENT" \
            $out/bin/wwn-iowatchdog
          /usr/bin/codesign --force -s - --entitlements "$ENT_CLAIM" \
            $out/bin/wwn-iowatchdog-claim
          /usr/bin/codesign --force -s - \
            $out/lib/libwwn_watchdogd_hook.dylib \
            $out/bin/wwn-iowatchdog-claim-install \
            $out/bin/wwn-iowatchdog-patha-amfi-nvram
        '';
        meta = with pkgs.lib; {
          description = "IOWatchdog Mode B tools (Path A entitled claim + Path B interpose; fail-closed soft-inject)";
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
        claim-install = {
          type = "app";
          program = "${self.packages.${system}.wwn-iowatchdog}/bin/wwn-iowatchdog-claim-install";
        };
        wwn-iowatchdog-claim-install = {
          type = "app";
          program = "${self.packages.${system}.wwn-iowatchdog}/bin/wwn-iowatchdog-claim-install";
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
