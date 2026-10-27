# Simulador del reloj control para Windows (MinGW de Strawberry, Ninja).
# Compilar: cmake -G Ninja -S sim -B .sim/build && ninja -C .sim/build
cmake_minimum_required(VERSION 3.16)
project(reloj_sim C CXX)

set(LIBDEPS "${CMAKE_CURRENT_SOURCE_DIR}/../.pio/libdeps/reloj" CACHE PATH "Librerías que bajó PlatformIO")
set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(SIM "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT EXISTS "${LIBDEPS}/lvgl/lvgl.h")
  message(FATAL_ERROR "Falta LVGL en ${LIBDEPS}. Compila una vez la placa (pio run) para que se baje.")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-O1)

file(GLOB_RECURSE LVGL_C "${LIBDEPS}/lvgl/src/*.c")
add_library(lvgl STATIC ${LVGL_C})
target_include_directories(lvgl PUBLIC "${SIM}" "${LIBDEPS}/lvgl")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(lvgl PRIVATE -w)

file(GLOB FUENTES "${SRC}/fuentes/*.c" "${SRC}/imagenes/*.c")
set(PROGRAMA
  "${SRC}/ui.cpp"
  "${SRC}/datos.cpp"
  "${SRC}/red.cpp"
  "${SRC}/panel.cpp"
  "${SRC}/fotos.cpp"
  "${SRC}/tema.cpp"
  "${SIM}/sim_http.cpp"
  "${SIM}/main_sim.cpp"
  "${SIM}/sim_arduino.cpp"
  "${SIM}/sim_webserver.cpp"
  "${SIM}/sim_huella.cpp"
  "${SIM}/sim_pantalla.cpp"
  "${SIM}/sim_panel.cpp"
  "${SIM}/sim_pruebas.cpp")
add_executable(reloj-sim WIN32 ${PROGRAMA} ${FUENTES})
# En la ESP32 timeval usa time_t de 64 bits; en Windows es long: se permite el recorte.
set_source_files_properties(${PROGRAMA} PROPERTIES COMPILE_OPTIONS "-include;forzado.h;-Wall;-Wno-narrowing")
target_include_directories(reloj-sim PRIVATE "${SIM}/arduino" "${SIM}" "${SRC}" "${LIBDEPS}/ArduinoJson/src")
target_compile_definitions(reloj-sim PRIVATE
  SIMULADOR
  _POSIX_THREAD_SAFE_FUNCTIONS=200112L
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STREAM=1
  ARDUINOJSON_ENABLE_ARDUINO_PRINT=1
  ARDUINOJSON_ENABLE_PROGMEM=0)
target_link_libraries(reloj-sim PRIVATE lvgl winhttp ws2_32 winmm gdi32 user32 imm32 -static)
