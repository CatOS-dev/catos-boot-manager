cmake_minimum_required(VERSION 3.26)
project(CatOSBootManager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent LinguistTools)

add_library(catos-boot-core STATIC
    src/model/BootTypes.h
    src/model/BootTransaction.h
    src/model/BootTransaction.cpp
    src/model/BootPlan.h
    src/model/BootPlan.cpp
    src/services/BootProbe.h
    src/services/BootProbe.cpp
    src/services/BootDiscovery.h
    src/services/BootDiscovery.cpp
    src/services/BootScan.h
    src/services/BootScan.cpp
    src/services/BootSettings.h
    src/services/BootSettings.cpp
    src/services/BootDevices.h
    src/services/BootDevices.cpp
    src/services/BootRepair.h
    src/services/BootRepair.cpp
)
target_include_directories(catos-boot-core PUBLIC src)
target_link_libraries(catos-boot-core PUBLIC Qt6::Core Qt6::Concurrent)

qt_add_executable(catos-boot-manager
    src/app/Main.cpp
    src/app/BootManagerWindow.h
    src/app/BootManagerWindow.cpp
    src/widgets/BootUi.h
    src/widgets/BootUi.cpp
    src/widgets/LoaderSelector.h
    src/widgets/LoaderSelector.cpp
    src/pages/OverviewPage.h
    src/pages/OverviewPage.cpp
    src/pages/MethodsPage.h
    src/pages/MethodsPage.cpp
    src/pages/SettingsPage.h
    src/pages/SettingsPage.cpp
    src/pages/MenuPage.h
    src/pages/MenuPage.cpp
    src/pages/FirmwarePage.h
    src/pages/FirmwarePage.cpp
    src/pages/PendingPage.h
    src/pages/PendingPage.cpp
    src/pages/RepairPage.h
    src/pages/RepairPage.cpp
    src/dialogs/ApplyDialog.h
    src/dialogs/ApplyDialog.cpp
)
target_link_libraries(catos-boot-manager PRIVATE catos-boot-core Qt6::Widgets)

qt_add_executable(catos-boot-apply src/privileged/BootApplyExecutor.cpp)
target_link_libraries(catos-boot-apply PRIVATE catos-boot-core)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(boot-transaction-test
        tests/BootTransactionTest.cpp
        src/pages/FirmwarePage.cpp
        src/pages/PendingPage.cpp
        src/widgets/BootUi.cpp
    )
    target_link_libraries(boot-transaction-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-transaction COMMAND boot-transaction-test)
    set_tests_properties(boot-transaction PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(boot-settings-draft-test
        tests/SettingsDraftTest.cpp
        src/pages/SettingsPage.cpp
        src/widgets/BootUi.cpp
        src/widgets/LoaderSelector.cpp
    )
    target_link_libraries(boot-settings-draft-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-settings-draft COMMAND boot-settings-draft-test)
    set_tests_properties(boot-settings-draft PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(boot-scan-test tests/BootScanTest.cpp)
    target_link_libraries(boot-scan-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-scan COMMAND boot-scan-test)
    qt_add_executable(boot-discovery-test tests/BootDiscoveryTest.cpp)
    target_link_libraries(boot-discovery-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-discovery COMMAND boot-discovery-test)
    qt_add_executable(boot-loader-selector-test tests/LoaderSelectorTest.cpp src/widgets/LoaderSelector.cpp)
    target_link_libraries(boot-loader-selector-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-loader-selector COMMAND boot-loader-selector-test)
    set_tests_properties(boot-loader-selector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(boot-methods-test tests/MethodsPageTest.cpp
        src/pages/MethodsPage.cpp src/widgets/LoaderSelector.cpp src/widgets/BootUi.cpp)
    target_link_libraries(boot-methods-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-methods COMMAND boot-methods-test)
    set_tests_properties(boot-methods PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

qt_add_lrelease(catos-boot-manager
    TS_FILES translations/catos-boot-manager_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE QM_FILES
)

install(TARGETS catos-boot-manager RUNTIME DESTINATION bin)
install(TARGETS catos-boot-apply RUNTIME DESTINATION lib/catos)
install(FILES data/catos-boot-manager.desktop DESTINATION share/applications)
install(FILES ${QM_FILES} DESTINATION share/catos-boot-manager/translations)
