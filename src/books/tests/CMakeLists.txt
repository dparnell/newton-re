# The books tests boot the OS (build/host/host/newton) and run a script in
# it: the librarian's state after the boot and a book of the script's own
# put in and taken out again.
add_test(NAME books.Library
    COMMAND newton --headless 6 --script ${CMAKE_CURRENT_SOURCE_DIR}/books-library.ns
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(books.Library PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "books-library: done, 0 failed")
# the same with the ROM's bugs (NEWTON_ROM_BUGS=1): the script checks the
# ROM's behaviour where a fix changes it (docs/rom-bugs.md)
add_test(NAME books.Library.romBugs
    COMMAND newton --headless 6 --script ${CMAKE_CURRENT_SOURCE_DIR}/books-library.ns
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(books.Library.romBugs PROPERTIES TIMEOUT 120 ENVIRONMENT NEWTON_ROM_BUGS=1
    PASS_REGULAR_EXPRESSION "books-library: done, 0 failed")

# the help book opened, a topic opened and a page drawn
add_test(NAME books.HelpBook
    COMMAND newton --display 320x480 --headless 12 --script ${CMAKE_CURRENT_SOURCE_DIR}/books-help.ns
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(books.HelpBook PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "books-help: done, 0 failed")

# Copperfield reading a book of the library's (a copy of the help book)
add_test(NAME books.Copperfield
    COMMAND newton --display 320x480 --headless 10 --script ${CMAKE_CURRENT_SOURCE_DIR}/books-copperfield.ns
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
set_tests_properties(books.Copperfield PROPERTIES TIMEOUT 120 PASS_REGULAR_EXPRESSION "books-copperfield: done, 0 failed")
