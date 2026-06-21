# components/opendroneid/CMakeLists.txt
idf_component_register(
    SRCS "opendroneid.c" "wifi.c"
    INCLUDE_DIRS "."
    REQUIRES driver
)

# 禁用 opendroneid 库内部 printf 格式警告（uint32_t 在 esp32s3 上为 long unsigned int）
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format)
