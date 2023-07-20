CURRENT_DIRECTORY=$(dirname $0)
BUILD_DIRECTORY="${CURRENT_DIRECTORY}/cmake-build-release"
CUSTOM_CMAKE="${1:-"/home/lwalcak/Documents/CMake_3.27.4/bin/cmake"}"

rm -rf $BUILD_DIRECTORY
$CUSTOM_CMAKE -S $CURRENT_DIRECTORY -B $BUILD_DIRECTORY -DCMAKE_BUILD_TYPE=Release || (echo "CMake generation errored with code: $?" && exit 1)
$CUSTOM_CMAKE --build $BUILD_DIRECTORY || (echo "CMake build errored with code: $?" && exit 2)

echo "Files built at \"$BUILD_DIRECTORY\""