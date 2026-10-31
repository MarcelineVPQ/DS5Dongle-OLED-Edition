# The board's name (identity [30..45], protocol 1.7) and board type id (0xEA
# [3..4], protocol 1.21) from boards.json, the one table of boards: the row
# whose pico_board is PICO_BOARD. A board not in the table keeps its
# PICO_BOARD id as its name and type id 0. Needs CMake 3.19 (string(JSON)).
# Included by CMakeLists.txt; also runs alone as a check:
#   cmake -DPICO_BOARD=pico_w -P cmake/ds5_board.cmake
set(DS5_BOARDS_JSON_FILE "${CMAKE_CURRENT_LIST_DIR}/../boards.json")
file(READ "${DS5_BOARDS_JSON_FILE}" _ds5_boards)
if (NOT CMAKE_SCRIPT_MODE_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DS5_BOARDS_JSON_FILE}")
endif ()
set(DS5_BOARD_NAME "${PICO_BOARD}")
set(DS5_BOARD_TYPE_ID 0)
string(JSON _ds5_n LENGTH "${_ds5_boards}" boards)
math(EXPR _ds5_last "${_ds5_n} - 1")
foreach (_ds5_i RANGE ${_ds5_last})
        string(JSON _ds5_pb GET "${_ds5_boards}" boards ${_ds5_i} pico_board)
        if (_ds5_pb STREQUAL PICO_BOARD)
                string(JSON DS5_BOARD_NAME GET "${_ds5_boards}" boards ${_ds5_i} name)
                string(JSON DS5_BOARD_TYPE_ID GET "${_ds5_boards}" boards ${_ds5_i} type_id)
        endif ()
endforeach ()
message(STATUS "Board: ${PICO_BOARD} = \"${DS5_BOARD_NAME}\", type id ${DS5_BOARD_TYPE_ID} (boards.json)")
