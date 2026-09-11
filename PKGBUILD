# Maintainer: JohnnyBoySou <joaodesousa101@gmail.com>
pkgname=bowser
pkgver=0.2.0
pkgrel=1
pkgdesc="Mini navegador CLI sobre a webview nativa do sistema (WebKitGTK), com screenshot e presets de viewport para agentes"
arch=('x86_64')
url="https://github.com/JohnnyBoySou/bowser"
license=('MIT')
depends=('webkit2gtk-4.1' 'gtk3')
makedepends=('go')
source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('732f1fef11e1412d24ea04b199968e95732d8cd9267339994499a71721020f90')

build() {
    cd "$pkgname-$pkgver"
    export CGO_ENABLED=1
    export CGO_CFLAGS="$CFLAGS"
    export CGO_LDFLAGS="$LDFLAGS"
    go build -trimpath -ldflags="-s -w" -o bowser .
}

package() {
    cd "$pkgname-$pkgver"
    install -Dm755 bowser "$pkgdir/usr/bin/bowser"
    install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
