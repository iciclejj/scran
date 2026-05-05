#
# This file serves as a template only. Hashes etc. are not necessarily kept up to date.
#
pkgname=scran
pkgver=0.9.0
pkgrel=1
pkgdesc='Image and video capture for Wayland'
arch=('x86_64')
url='https://github.com/iciclejj/scran'
license=('MIT AND OFL-1.1')
makedepends=('wayland-protocols')
depends=(
    'wayland>=1.14.91'
    'libxkbcommon'
    'libsystemd'
    'ffmpeg'
    'libpipewire'
    'blend2d'
)

source=("$pkgname-$pkgver.tar.gz::https://github.com/iciclejj/$pkgname/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('b7d3e266c2084ee1457e011c8d7e6162d99c19d5d7e7ee1a066f15fc2bc80134')

build() {
    cd "$srcdir/$pkgname-$pkgver"

    make release SD_BUS_LIB=libsystemd
}

package() {
    cd "$srcdir/$pkgname-$pkgver"

    install -D -m 755 "./build/release/$pkgname" "$pkgdir/usr/bin/$pkgname"

    install -D -m 644 "./LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    install -D -m 644 "./assets/Iosevka.license" "$pkgdir/usr/share/licenses/$pkgname/Iosevka.license"
    install -D -m 644 "./assets/NerdFontsSymbolsOnly.license" "$pkgdir/usr/share/licenses/$pkgname/NerdFontsSymbolsOnly.license"
}
