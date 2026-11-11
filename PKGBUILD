# Maintainer: kaiole <https://github.com/kaiole>

# Builds the main branch from GitHub, not the directory this file is in.
#
# The plugin only loads into the Hyprland it was built against: build and
# install this package again after every Hyprland update.

pkgname=hyprcast-git
pkgver=r0
pkgrel=1
pkgdesc='Shows keystrokes on screen in Hyprland'
arch=('x86_64')
url='https://github.com/kaiole/hyprcast'
license=('MIT')
depends=(
  'hyprland'
  'layer-shell-qt'
  'libxkbcommon'
  'qt6-base'
  'qt6-declarative'
  'tomlplusplus'
)
makedepends=('cmake' 'git' 'ninja')
provides=('hyprcast')
conflicts=('hyprcast')

# The libjson commit pinned in plugin/CMakeLists.txt. makepkg downloads it
# here so that the build itself needs no network; prepare() fails if the two
# pins differ.
_libjson=013397b90735d630d89eab79c2d097bf2ec9876d

source=(
  "hyprcast::git+$url.git"
  "libjson-$_libjson.tar.gz::https://github.com/kaiole/libjson/archive/$_libjson.tar.gz"
)
sha256sums=(
  'SKIP'
  'f2c86456ba8d0ea0b5ad94ae2c562f63532116455a208d5616149ad27be3ea36'
)

pkgver() {
  cd hyprcast
  printf 'r%s.%s' "$(git rev-list --count HEAD)" "$(git rev-parse --short=7 HEAD)"
}

prepare() {
  if ! grep -q "$_libjson" hyprcast/plugin/CMakeLists.txt; then
    echo "PKGBUILD pins libjson $_libjson, plugin/CMakeLists.txt another commit" >&2
    return 1
  fi
}

build() {
  cd hyprcast
  # The release preset is what the README builds; it writes to build/release.
  cmake --preset release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_LIBJSON="$srcdir/libjson-$_libjson"
  cmake --build --preset release
}

package() {
  cd hyprcast
  DESTDIR="$pkgdir" cmake --install build/release
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
