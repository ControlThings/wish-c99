cd ..;
VERSION=`git describe --tags --dirty`;
if [ -z "$VERSION" ]; then
VERSION=`cat VERSION`
fi
mkdir build;
cd build;
export CC=arm-linux-gnueabihf-gcc 
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_IA32=OFF ..;
make;
TARGET=wish-core-${VERSION}-arm-linux
cp wish-core $TARGET
arm-linux-gnueabihf-strip $TARGET;
cd ../tools
