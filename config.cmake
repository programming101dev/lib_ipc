# Project metadata
set(PROJECT_NAME "p101_ipc")
set(PROJECT_VERSION "0.0.1")
set(PROJECT_DESCRIPTION "Pipes, FIFOs, shared memory, and System V IPC")
set(PROJECT_LANGUAGE "C")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(STANDARD_FLAGS
        -D_POSIX_C_SOURCE=200809L
        -D_XOPEN_SOURCE=700
        -Werror
)
set(DARWIN_STANDARD_FLAGS -D_DARWIN_C_SOURCE)
set(LINUX_STANDARD_FLAGS -D_GNU_SOURCE)
set(BSD_STANDARD_FLAGS -D_BSD_SOURCE -D__BSD_VISIBLE)

set(LIBRARY_TARGETS p101_ipc)
set(p101_ipc_SOURCES
        src/posix/sys/stat.c
        src/posix/unistd.c
        src/posix_optional/sys/mman.c
        src/posix_xsi/sys/ipc.c
        src/posix_xsi/sys/msg.c
        src/posix_xsi/sys/sem.c
        src/posix_xsi/sys/shm.c
)
set(p101_ipc_HEADERS
        include/p101_ipc/ipc.h
)
set(p101_ipc_LINK_LIBRARIES
        p101_error
        p101_env
        p101_c
)

