{
  # Tailor created this file. It no longer uses Tailor's nix/ fragments
  # (loader.nix, hooks.nix): isospin has no Tailor language enabled, and the
  # GPU driver hook has no use here. Tailor writes nix/ again on each
  # `tailor alter`, so .gitignore ignores it.
  description = "Development shell for isospin";

  inputs = {
    # The same nixpkgs revision as NIXPKGS in .github/workflows/check.yml and
    # sc-plugin.yml (Faust 2.85.9). Then a local build gives the same bytes as a
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
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          # Keep the development shell available when Tailor has no package for this system.
          tailorPkgs = nix-packages.packages.${system} or { };
        in
        {
          default = pkgs.mkShell {
            packages =
              with pkgs;
              [
                actionlint
                gh
                just
                # The isospin build: Faust, Python for the scripts (standard
                # library only), and a C++ compiler for scripts/check-native.py.
                # The scripts import md2misc from the external/md2misc
                # submodule. md2misc also needs only the standard library.
                # CMake builds the loadable plugin (scripts/build-sc-plugin.py).
                faust
                python3
                gcc
                cmake
              ]
              ++ (if tailorPkgs ? tailor then [ tailorPkgs.tailor ] else [ ]);
          };
        }
      );
    };
}
