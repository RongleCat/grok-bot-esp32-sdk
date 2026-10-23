idf_component_register(SRCS "src/grok_bot.cpp" "src/avatar.cpp" "src/esp_transport.cpp"
    INCLUDE_DIRS "include" REQUIRES json esp_http_client mbedtls esp_hw_support esp-tls)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
