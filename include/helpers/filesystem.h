// Prior to GCC 9, filesystem is not available in std, but in std::experimental
#if defined(__GNUG__) && !defined(__clang__)
// Include to have GCC feature test macros
#   include <features.h>
// If GCC 8+, filesystem is available in std
#   if __GNUC_PREREQ(9,0)
#       include <filesystem>
        namespace fs = std::filesystem;
#   else
// else, use experimental one instead
#       include <experimental/filesystem>
        namespace fs = std::experimental::filesystem;
#   endif
#else
// Other compilers (or at least Clang as far as I know) have std::filesystem available in C++17
#   include <filesystem>
    namespace fs = std::filesystem;
#endif

using Path = fs::path;