cask "crowsnest" do
  version "0.6.0"
  sha256 :no_check

  url "https://github.com/codeafridi/Crowsnest/releases/download/v#{version}/Crowsnest-#{version}-macos.dmg"
  name "Crowsnest"
  desc "Cross-platform overlay system monitor and reliability HUD for local machines"
  homepage "https://github.com/codeafridi/Crowsnest"
  conflicts_with cask: "syspeek"

  livecheck do
    url :homepage
    strategy :github_latest
  end

  depends_on macos: ">= :monterey"

  app "Crowsnest.app"

  # The build is currently unsigned. After the first launch macOS may
  # block it; either right-click -> Open once, or run:
  #   xattr -d com.apple.quarantine /Applications/Crowsnest.app
end
