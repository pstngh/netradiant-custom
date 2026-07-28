# NetRadiant Custom for macOS

The Apple build is a native Qt 5 application. It does not require XQuartz or
MacPorts.

Install the build and packaging dependencies with Homebrew:

```sh
brew install qt@5 glib libxml2 libpng jpeg-turbo assimp@5 pkgconf dylibbundler
```

Set `PATH` and `PKG_CONFIG_PATH` for the keg-only dependencies, then build:

```sh
QT_PREFIX="$(brew --prefix qt@5)"
ASSIMP_PREFIX="$(brew --prefix assimp@5)"
JPEG_PREFIX="$(brew --prefix jpeg-turbo)"

export PATH="$QT_PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$QT_PREFIX/lib/pkgconfig:$ASSIMP_PREFIX/lib/pkgconfig:$(brew --prefix glib)/lib/pkgconfig:$(brew --prefix libxml2)/lib/pkgconfig:$(brew --prefix libpng)/lib/pkgconfig:$JPEG_PREFIX/lib/pkgconfig"

make CC=clang CXX=clang++ \
  CPPFLAGS_JPEG="-I$JPEG_PREFIX/include" \
  LIBS_JPEG="-L$JPEG_PREFIX/lib -ljpeg" \
  DOWNLOAD_GAMEPACKS=no

bash setup/apple/package.sh
```

The packaging script creates:

- `setup/apple/target/NetRadiant-Custom.app`
- `setup/apple/target/NetRadiant-Custom-macos-<arch>.zip`

It deploys Qt with `macdeployqt`, collects other Homebrew dylibs with
`dylibbundler`, checks for build-machine dependency paths, and applies an ad-hoc
signature. Release builds are not notarized; users may need to use Finder's
**Open** command the first time they launch a downloaded build.
