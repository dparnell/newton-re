# Turns a text file into a C++ source defining it as a string, so a host
# program carries NewtonScript source of its own without reading a file at
# run time (from any directory).  Run as a script:
#
#   cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DNAME=<identifier> -P EmbedText.cmake
#
# OUTPUT defines `extern const char NAME[]`, the whole of INPUT (as a raw
# string literal: INPUT must not contain the delimiter )__EMBED__").
file(READ "${INPUT}" text)
file(WRITE "${OUTPUT}" "// Generated from ${INPUT} by src/cmake/EmbedText.cmake - do not edit.\nextern const char ${NAME}[];\nconst char ${NAME}[] = R\"__EMBED__(${text})__EMBED__\";\n")
