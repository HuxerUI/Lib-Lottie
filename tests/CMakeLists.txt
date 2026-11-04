find_package(HuxerUI CONFIG REQUIRED COMPONENTS static testing)
add_executable(lottie_tests lottie_tests.cpp)
target_compile_features(lottie_tests PRIVATE cxx_std_20)
target_include_directories(lottie_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(lottie_tests PRIVATE HuxerUI::Lottie HuxerUI::testing)
huxerui_enable_codegen(lottie_tests)
huxerui_add_resources(lottie_tests ROOT "${CMAKE_CURRENT_SOURCE_DIR}/resources" NAMESPACE fixtures)
target_compile_definitions(lottie_tests PRIVATE
    TEST_PACKAGE_DIR="${CMAKE_CURRENT_BINARY_DIR}/huxerui-resources/lottie_tests/package"
    TEST_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/resources/raw/motion.json"
)
add_test(NAME lottie COMMAND lottie_tests)
set_tests_properties(lottie PROPERTIES TIMEOUT 30)
