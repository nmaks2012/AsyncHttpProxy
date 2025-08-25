cmake_minimum_required(VERSION 3.31.6)
project(AsyncHttpProxy VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Все предупреждения - это ошибки
    add_compile_options(-Wfatal-errors -Wall -Werror)

    # Не больше одной ошибки за раз
    add_compile_options(-fmax-errors=1)
endif()

set(CMAKE_PREFIX_PATH /usr/local/asio)

# Ищем необходимые библиотеки
find_package(Boost REQUIRED)
find_package(GTest REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB HEADER_FILES "${CMAKE_SOURCE_DIR}/include/*.hpp")


# Создаём статическую библиотеку
add_library(${PROJECT_NAME}_imp STATIC
  "${CMAKE_SOURCE_DIR}/src/main.cpp"
  "${CMAKE_SOURCE_DIR}/src/headers.cpp"
  ${HEADER_FILES})

# Добавляем в проект используемые сторонние библиотеки
target_include_directories(${PROJECT_NAME}_imp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${Boost_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME}_imp PRIVATE ${Boost_LIBRARIES})

# Создаём исполняемый таргет и линкуем к нему статическую библиотеку
add_executable(${PROJECT_NAME}
  "${CMAKE_SOURCE_DIR}/src/main.cpp"
  "${CMAKE_SOURCE_DIR}/src/headers.cpp")
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_imp)

#
# Тесты
#

# Рекурсивно ищем все используемые в тестах .cpp файлы
file(GLOB TEST_SRC_FILES "${CMAKE_SOURCE_DIR}/tests/*.cpp")

add_executable(${PROJECT_NAME}_tests "${TEST_SRC_FILES}")
target_link_libraries(${PROJECT_NAME}_tests PRIVATE ${PROJECT_NAME}_imp GTest::GTest GTest::Main)

# Включаем тестирование
enable_testing()
add_test(NAME AsyncHttpProxy_Tests COMMAND ${PROJECT_NAME}_tests)

# Интергационные тесты
add_test(
    NAME Integration.PytestProxyTest
    COMMAND
        ${Python3_EXECUTABLE} -m pytest
        -v
        --color=yes
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/proxy_test.py
)

set_property(TARGET ${PROJECT_NAME} PROPERTY FIXTURE_SETUP "proxy_executable_fixture")

set_tests_properties(Integration.PytestProxyTest
    PROPERTIES
        # Запускаем тест из папки build
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        # Устанавливаем переменную окружения PROXY_EXECUTABLE
        ENVIRONMENT "PROXY_EXECUTABLE=$<TARGET_FILE:AsyncHttpProxy>"
        FIXTURES_REQUIRED "proxy_executable_fixture"
)