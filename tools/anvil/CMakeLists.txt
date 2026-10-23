add_library(ManroAnvil STATIC
        Nbt.cpp
        RegionFile.cpp
        AnvilSource.cpp
)

target_include_directories(ManroAnvil PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/engine/include
)
target_link_libraries(ManroAnvil PUBLIC ZLIB::ZLIB)
