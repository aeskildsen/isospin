{
  description = "Development shell for isospin";

  inputs = {
    # The same nixpkgs revision as NIXPKGS in .github/workflows/check.yml and
    # release.yml (Faust 2.85.9). Then a local build gives the same bytes as a
    # release. Change the revision only on purpose, and in all three files: a
    # new Faust version can change the generated sources.
    nixpkgs.url = "github:NixOS/nixpkgs/7fd4004d0511991fff3a0524856a9be09e28c5f0";
    nix-packages.url = "github:wimpysworld/nix-packages";
    nix-packages.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    {
      nixpkgs,
      nix-packages,
      ...
    }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-darwin"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        pkgs.lib.optionalAttrs (builtins.pathExists ./nix/lib/blender.nix) {
          blender-agent-bridge-extension = (import ./nix/lib/blender.nix { inherit pkgs; }).extension;
        }
      );
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          # Keep the development shell available when Tailor has no package for this system.
          tailorPkgs = nix-packages.packages.${system} or { };
        in
        {
          default = pkgs.mkShell {
            shellHook = import ./nix/hooks.nix { inherit pkgs; };
            packages =
              with pkgs;
              [
                actionlint
                gh
                just
                # The isospin build: Faust, Python for the scripts (standard
                # library only), and a C++ compiler for scripts/check-native.py.
                faust
                python3
                gcc
              ]
              ++ (if tailorPkgs ? tailor then [ tailorPkgs.tailor ] else [ ])
              ++ import ./nix/loader.nix { inherit pkgs; };
          };
        }
        //
          pkgs.lib.optionalAttrs
            (system == "x86_64-linux" && builtins.pathExists ./nix/shells/go-ebitengine-android.nix)
            {
              android = import ./nix/shells/go-ebitengine-android.nix { inherit pkgs; };
            }
      );
    };
}
