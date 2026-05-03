set(git_commit "unknown")
set(git_commit_short "unknown")

if(BRIEFUTIL_GIT_EXECUTABLE)
    execute_process(
        COMMAND "${BRIEFUTIL_GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${BRIEFUTIL_SOURCE_DIR}"
        OUTPUT_VARIABLE git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${BRIEFUTIL_GIT_EXECUTABLE}" rev-parse --short=8 HEAD
        WORKING_DIRECTORY "${BRIEFUTIL_SOURCE_DIR}"
        OUTPUT_VARIABLE git_commit_short
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${BRIEFUTIL_GIT_EXECUTABLE}" diff-index --quiet HEAD --
        WORKING_DIRECTORY "${BRIEFUTIL_SOURCE_DIR}"
        RESULT_VARIABLE git_dirty
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(git_dirty EQUAL 1)
        string(APPEND git_commit_short "+dirty")
    endif()
endif()

string(TIMESTAMP build_timestamp "%Y-%m-%d %H:%M:%S %z")
string(TIMESTAMP build_timestamp_compact "%Y-%m-%d %H:%M")

set(content
"[build]
version=${BRIEFUTIL_VERSION}
git_commit=${git_commit}
git_commit_short=${git_commit_short}
build_timestamp=${build_timestamp}
build_timestamp_compact=${build_timestamp_compact}
")

file(WRITE "${BRIEFUTIL_BUILD_INFO_FILE}.tmp" "${content}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${BRIEFUTIL_BUILD_INFO_FILE}.tmp"
        "${BRIEFUTIL_BUILD_INFO_FILE}"
)
file(REMOVE "${BRIEFUTIL_BUILD_INFO_FILE}.tmp")
