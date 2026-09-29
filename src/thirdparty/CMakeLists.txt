# Host re-expressions of third-party packages' native code (the owner's
# decision: a package's native functions are re-expressed as host code where
# it matters, frames/PackageNatives.h; anything else falls back on the ARM
# interpreter).
add_subdirectory(nie)
