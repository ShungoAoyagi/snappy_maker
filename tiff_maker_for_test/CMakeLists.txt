cmake_minimum_required(VERSION 3.10)
project(tiff_maker_for_test)

# C++17 を必須とする設定
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")

# MinGWの場合、-pthread フラグを追加する
if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

# リンカオプションに静的リンク用のフラグを追加
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

find_package(Threads REQUIRED)


# 実行ファイルの作成
add_executable(tiff_maker_for_test tiff_maker_for_test.cpp)
# target_link_libraries(SnappyToMergedTif snappy archive tiff)
target_link_libraries(tiff_maker_for_test)

# # デバッグ情報の出力
# message(STATUS "Archive library: ${archive_LIBRARIES}")
# message(STATUS "Archive include: ${archive_INCLUDE_DIRS}")