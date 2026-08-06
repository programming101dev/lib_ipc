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
        src/sys/ipc.c
        src/sys/mman.c
        src/sys/msg.c
        src/sys/sem.c
        src/sys/shm.c
        src/sys/stat.c
        src/unistd.c
)
set(p101_ipc_HEADERS
        include/p101_ipc/p101_unistd.h
        include/p101_ipc/sys/p101_ipc.h
        include/p101_ipc/sys/p101_mman.h
        include/p101_ipc/sys/p101_msg.h
        include/p101_ipc/sys/p101_sem.h
        include/p101_ipc/sys/p101_shm.h
        include/p101_ipc/sys/p101_stat.h
)
set(p101_ipc_LINK_LIBRARIES
        p101_error
        p101_env
        p101_c
)

