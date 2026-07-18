idf_component_register(
    SRCS
        "cuktech_protocol.c"
        "crypto_mbedtls.c"
        "miot_tlv.c"
        "port_decode.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES mbedtls
)
