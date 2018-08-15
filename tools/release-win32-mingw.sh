cd ..;
VERSION=`git describe`;
if [ -z "$VERSION" ]; then
VERSION=`cat VERSION`
fi
mkdir build;
cd build;
cmake -DBUILD_WIN32=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../tools/Toolchain-mingw.cmake ..
make;
TARGET=wish-core-${VERSION}-win32-mingw.exe
cp wish-core.exe $TARGET
strip $TARGET;

cd ../tools

