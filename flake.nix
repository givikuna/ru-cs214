{
  description = "CS214 w Menny";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
    in
    {
      devShells.${system}.default =
        let
          pkgs = import nixpkgs { inherit system; };

          mkScript =
            command-name: file-path:
            let
              interpreter =
                let
                  ext =
                    let
                      match = builtins.match ".*\\.([^.]+)$" file-path;
                    in
                    if match != null then builtins.head match else throw "couldn't determine ext";
                in
                {
                  "sh" = "${pkgs.bash}/bin/bash";
                  "elv" = "${pkgs.elvish}/bin/elvish";
                  "py" = "${pkgs.python3}/bin/python3";
                  "js" = "${pkgs.nodejs}/bin/node";
                  "nu" = "${pkgs.nushell}/bin/nu";
                }
                .${ext} or (throw "unsupported .${ext}");
            in
            pkgs.writeShellScriptBin command-name ''
              ROOT_DIR=$(git rev-parse --show-toplevel 2>/dev/null || pwd)
              exec ${interpreter} "$ROOT_DIR/${file-path}" "$@"
            '';
        in
        pkgs.mkShell {
          buildInputs = with pkgs; [
            gcc
            gnumake
            gdb
            valgrind
            clang-tools

            nushell

            # scripts

            (mkScript "tarnu" "scripts/tarnu.nu")
          ];
        };
    };
}
