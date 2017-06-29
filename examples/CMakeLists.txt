set(SOURCES
    raft_server_example.cxx)

add_executable(raft_server_example ${SOURCES})
target_link_libraries(raft_server_example cornerstone)
