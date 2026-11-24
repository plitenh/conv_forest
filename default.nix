{ lib
, stdenv
, pkg-config
, curl
, sqlite
, cjson
, openssl
}:

stdenv.mkDerivation {
  pname = "conv_forest";
  version = "0.1.0";

  src = ./.;

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ curl.dev sqlite.dev cjson openssl.dev ];

  buildPhase = "make";
  installPhase = ''
    runHook preInstall
    install -Dm755 dpsk_capture $out/bin/dpsk_capture
    install -Dm755 grok_ui_agent $out/bin/grok_ui_agent
    runHook postInstall
  '';

  meta = {
    description = "DeepSeek web conversation forest backup tool";
    mainProgram = "dpsk_capture";
    platforms = lib.platforms.linux;
  };
}
