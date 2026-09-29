# The book reader's C++ side (Librarian.h): TLibrarian, the library of
# installed books the ROM's NewtonScript reader (Copperfield) asks about,
# its NewtonScript functions, and the 'book part handler.  Over the view
# system (the reader is the root's copperfield), the stores (the Library
# soup), the Assistant (a book's task templates) and the frames part
# handlers' import of a part's objects (packages).
add_library(books STATIC
    Librarian.cpp
    BookPartHandler.cpp
)
target_link_libraries(books PUBLIC newton_config frames views stores assist packages package_manager)
target_include_directories(books PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


add_subdirectory(tests)
