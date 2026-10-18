cask "syspeek" do
  version "0.6.0"
  sha256 :no_check

  url "https://github.com/codeafridi/Syspeek/releases/download/v#{version}/Syspeek-#{version}-macos.dmg"
  name "Syspeek"
  desc "Cross-platform overlay system monitor and reliability HUD for local machines"
  homepage "https://github.com/codeafridi/Syspeek"

  livecheck do
    url :homepage
    strategy :github_latest
  end

  depends_on macos: ">= :monterey"

  app "Syspeek.app"

  # The build is currently unsigned. After the first launch macOS may
  # block it; either right-click -> Open once, or run:
  #   xattr -d com.apple.quarantine /Applications/Syspeek.app
end
