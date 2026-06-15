idf_component_register(
    SRCS 
        "hello_world_main.c"
        "sbw_jtag.c"
        "sbw_device.c"
        "sbw_transport.c"
        "usb_descriptors.c"
    INCLUDE_DIRS "../include"
    REQUIRES tinyusb esp_hw_support freertos esp_driver_uart
    PRIV_REQUIRES spi_flash
)
