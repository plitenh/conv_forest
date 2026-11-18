{
  description = "conv_forest: DeepSeek web conversation forest backup tool";

  inputs.nixpkgs.url = "nixpkgs";

  outputs = { self, nixpkgs }: {
    packages.x86_64-linux.default = nixpkgs.legacyPackages.x86_64-linux.callPackage ./default.nix { };
    devShells.x86_64-linux.default = nixpkgs.legacyPackages.x86_64-linux.callPackage ./shell.nix { };
  };
}
