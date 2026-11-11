#auto format source code
function(fix_format path)
  find_program(CLANG clang-format /usr/bin /usr/local/bin /bin /sbin)
  if(CLANG)
    message(STATUS "clang found[${CLANG}]")
    foreach(arg IN LISTS ARGN)
      message(STATUS "fixing format for: ${path}/${arg}")
      set(CMD "${CLANG} -style=LLVM -i ${path}/${arg}")
      message(STATUS "${CMD}")
      execute_process(
        COMMAND ${CLANG} -style=Chromium -i ${path}/${arg}
        RESULT_VARIABLE result
      )
      if(result)
        message(STATUS "${CLANG} command failed. Result: ${result}")
      endif()
    endforeach()
  else()
    message(STATUS "Unable to format source -> clang-format not found: ${CLANG}")
  endif()
endfunction()
