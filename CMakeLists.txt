cmake_minimum_required(VERSION 3.26)
project(CatOSBootManager VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent LinguistTools)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EFIBOOT REQUIRED IMPORTED_TARGET efiboot efivar)

set(LIMINE_TOOL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../limine-tool" CACHE PATH "Limine parser source")
find_program(CARGO_EXECUTABLE cargo REQUIRED)
configure_file(tools/limine-menu/Cargo.toml.in limine-menu/Cargo.toml @ONLY)
configure_file(tools/limine-menu/Cargo.lock limine-menu/Cargo.lock COPYONLY)
file(GLOB_RECURSE LIMINE_PARSER_SOURCES CONFIGURE_DEPENDS "${LIMINE_TOOL_SOURCE}/src/*.rs")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/catos-limine-menu"
    COMMAND ${CARGO_EXECUTABLE} build --locked --release --manifest-path "${CMAKE_CURRENT_BINARY_DIR}/limine-menu/Cargo.toml" --target-dir "${CMAKE_CURRENT_BINARY_DIR}/menu-tools"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/menu-tools/release/catos-limine-menu" "${CMAKE_CURRENT_BINARY_DIR}/catos-limine-menu"
    DEPENDS tools/limine-menu/src/main.rs tools/limine-menu/Cargo.toml.in tools/limine-menu/Cargo.lock ${LIMINE_PARSER_SOURCES}
    VERBATIM)
add_custom_target(catos-menu-tools ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/catos-limine-menu")

add_library(catos-boot-core STATIC
    src/model/BootTypes.h
    src/model/BootSnapshotCodec.h
    src/model/BootSnapshotCodec.cpp
    src/model/BootTransaction.h
    src/model/BootTransaction.cpp
    src/model/BootPlan.h
    src/model/BootPlan.cpp
    src/services/BootProbe.h
    src/services/BootProbe.cpp
    src/services/BootDiscovery.h
    src/services/BootDiscovery.cpp
    src/services/BootPackages.h
    src/services/BootPackages.cpp
    src/services/BootSecurity.h
    src/services/BootSecurity.cpp
    src/services/BootBackup.h
    src/services/BootBackup.cpp
    src/services/BootContent.h
    src/services/BootContent.cpp
    src/services/BootMenus.h
    src/services/BootMenus.cpp
    src/services/BootDeploy.h
    src/services/BootDeploy.cpp
    src/services/BootBls.h
    src/services/BootBls.cpp
    src/services/BootHealth.h
    src/services/BootHealth.cpp
    src/services/BootScan.h
    src/services/BootScan.cpp
    src/services/BootSession.h
    src/services/BootSession.cpp
    src/services/BootSettings.h
    src/services/BootSettings.cpp
    src/services/BootDevices.h
    src/services/BootDevices.cpp
    src/services/BootRepair.h
    src/services/BootRepair.cpp
)
target_include_directories(catos-boot-core PUBLIC src)
target_link_libraries(catos-boot-core PUBLIC Qt6::Core Qt6::Concurrent PkgConfig::EFIBOOT)
add_dependencies(catos-boot-core catos-menu-tools)

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
    src/pages/SecurityPage.h
    src/pages/SecurityPage.cpp
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
    qt_add_executable(boot-window-test tests/BootManagerWindowTest.cpp
        src/app/BootManagerWindow.cpp src/dialogs/ApplyDialog.cpp
        src/pages/OverviewPage.cpp src/pages/MethodsPage.cpp src/pages/SecurityPage.cpp
        src/pages/MenuPage.cpp src/pages/SettingsPage.cpp src/pages/FirmwarePage.cpp
        src/pages/PendingPage.cpp src/pages/RepairPage.cpp
        src/widgets/LoaderSelector.cpp src/widgets/BootUi.cpp)
    target_link_libraries(boot-window-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-window COMMAND boot-window-test)
    set_tests_properties(boot-window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(boot-menus-test tests/BootMenusTest.cpp)
    target_link_libraries(boot-menus-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-menus COMMAND boot-menus-test)
    qt_add_executable(boot-deploy-test tests/BootDeployTest.cpp)
    target_link_libraries(boot-deploy-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-deploy COMMAND boot-deploy-test)
    qt_add_executable(boot-session-test tests/BootSessionTest.cpp)
    target_link_libraries(boot-session-test PRIVATE catos-boot-core Qt6::Test)
    add_dependencies(boot-session-test catos-boot-apply catos-boot-manager)
    add_test(NAME boot-session COMMAND boot-session-test)
    qt_add_executable(boot-install-test tests/BootInstallTest.cpp)
    target_link_libraries(boot-install-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-install COMMAND boot-install-test)
    qt_add_executable(boot-overview-test tests/OverviewPageTest.cpp src/pages/OverviewPage.cpp src/widgets/BootUi.cpp)
    target_link_libraries(boot-overview-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-overview COMMAND boot-overview-test)
    set_tests_properties(boot-overview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(boot-copy-test tests/BootCopyTest.cpp src/widgets/BootUi.cpp)
    target_link_libraries(boot-copy-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-copy COMMAND boot-copy-test)
    set_tests_properties(boot-copy PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
    qt_add_executable(boot-packages-test tests/BootPackagesTest.cpp)
    target_link_libraries(boot-packages-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-packages COMMAND boot-packages-test)
    qt_add_executable(boot-security-test tests/BootSecurityTest.cpp)
    target_link_libraries(boot-security-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-security COMMAND boot-security-test)
    qt_add_executable(boot-backup-test tests/BootBackupTest.cpp)
    target_link_libraries(boot-backup-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-backup COMMAND boot-backup-test)
    qt_add_executable(boot-health-test tests/BootHealthTest.cpp)
    target_link_libraries(boot-health-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-health COMMAND boot-health-test)
    qt_add_executable(boot-content-test tests/BootContentTest.cpp)
    target_link_libraries(boot-content-test PRIVATE catos-boot-core Qt6::Test)
    add_test(NAME boot-content COMMAND boot-content-test)
    qt_add_executable(boot-systemd-menu-test tests/SystemdMenuTest.cpp
        src/pages/MenuPage.cpp src/pages/SettingsPage.cpp src/widgets/LoaderSelector.cpp src/widgets/BootUi.cpp)
    target_link_libraries(boot-systemd-menu-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-systemd-menu COMMAND boot-systemd-menu-test)
    set_tests_properties(boot-systemd-menu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(boot-loader-selector-test tests/LoaderSelectorTest.cpp src/widgets/LoaderSelector.cpp src/widgets/BootUi.cpp)
    target_link_libraries(boot-loader-selector-test PRIVATE catos-boot-core Qt6::Widgets Qt6::Test)
    add_test(NAME boot-loader-selector COMMAND boot-loader-selector-test)
    set_tests_properties(boot-loader-selector PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(boot-methods-test tests/MethodsPageTest.cpp
        src/pages/MethodsPage.cpp src/pages/RepairPage.cpp src/widgets/LoaderSelector.cpp src/widgets/BootUi.cpp)
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
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/catos-limine-menu" DESTINATION lib/catos)
install(FILES data/catos-boot-manager.desktop DESTINATION share/applications)
install(FILES ${QM_FILES} DESTINATION share/catos-boot-manager/translations)
