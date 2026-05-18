cmake_minimum_required(VERSION 3.16)

project(fileMenager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FILEMENAGER_SOURCES
    app/main.cpp
    app/Application.cpp
    app/ConsoleInterface.cpp
    core/BackupCatalog.cpp
    core/BackupTypes.cpp
    core/BackupUtils.cpp
    core/DevTrace.cpp
    core/DaemonUtils.cpp
    core/MetadataRepository.cpp
    core/TaskQueue.cpp
    tasks/AddFileTask.cpp
    tasks/ModifyFileTask.cpp
    tasks/DeleteFileTask.cpp
    tasks/RestoreFileTask.cpp
    tasks/RepairBackupTask.cpp
    workers/BackupWorker.cpp
    workers/FolderMonitor.cpp
)

set(FILEMENAGER_HEADERS
    app/Application.h
    app/ConsoleInterface.h
    core/BackupCatalog.h
    core/BackupTaskContext.h
    core/BackupTypes.h
    core/BackupUtils.h
    core/DevTrace.h
    core/DaemonUtils.h
    core/MetadataRepository.h
    core/TaskQueue.h
    tasks/AddFileTask.h
    tasks/DeleteFileTask.h
    tasks/IBackupTask.h
    tasks/ModifyFileTask.h
    tasks/RestoreFileTask.h
    tasks/RepairBackupTask.h
    workers/BackupWorker.h
    workers/FolderMonitor.h
)

set(FILEMENAGER_PROJECT_FILES
    README.md
    CMakeLists.txt
)

add_executable(fileMenager
    ${FILEMENAGER_SOURCES}
    ${FILEMENAGER_HEADERS}
    ${FILEMENAGER_PROJECT_FILES}
)

target_include_directories(fileMenager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(fileMenager PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME smoke COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/smoke_test.sh")

include(GNUInstallDirs)
install(TARGETS fileMenager
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
