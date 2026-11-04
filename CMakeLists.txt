cmake_minimum_required(VERSION 3.26)
project(CatOSBootManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets LinguistTools)

add_library(catos-boot-core STATIC
    src/model/BootTypes.h
    src/model/BootTransaction.h
    src/model/BootTransaction.cpp
    src/services/BootProbe.h
    src/services/BootProbe.cpp
    src/services/BootSettings.h
    src/services/BootSettings.cpp
)
target_include_directories(catos-boot-core PUBLIC src)
target_link_libraries(catos-boot-core PUBLIC Qt6::Core)

qt_add_executable(catos-boot-manager
    src/app/Main.cpp
    src/app/BootManagerWindow.h
    src/app/BootManagerWindow.cpp
    src/widgets/BootUi.h
    src/widgets/BootUi.cpp
    src/pages/OverviewPage.h
    src/pages/OverviewPage.cpp
    src/pages/SettingsPage.h
    src/pages/SettingsPage.cpp
    src/pages/MenuPage.h
    src/pages/MenuPage.cpp
    src/pages/FirmwarePage.h
    src/pages/FirmwarePage.cpp
    src/pages/RepairPage.h
    src/pages/RepairPage.cpp
    src/dialogs/ApplyDialog.h
    src/dialogs/ApplyDialog.cpp
)
target_link_libraries(catos-boot-manager PRIVATE catos-boot-core Qt6::Widgets)

qt_add_executable(catos-boot-apply src/privileged/BootApplyExecutor.cpp)
target_link_libraries(catos-boot-apply PRIVATE catos-boot-core)

qt_add_lrelease(catos-boot-manager
    TS_FILES translations/catos-boot-manager_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)

install(TARGETS catos-boot-manager RUNTIME DESTINATION bin)
install(TARGETS catos-boot-apply RUNTIME DESTINATION lib/catos)
install(FILES data/catos-boot-manager.desktop DESTINATION share/applications)
install(FILES ${QM_FILES} DESTINATION share/catos-boot-manager/translations)
