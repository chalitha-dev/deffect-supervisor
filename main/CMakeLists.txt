file(GLOB_RECURSE SRC_UI "ui/*.c")

# Register everything to the ESP-IDF component builder
idf_component_register(SRCS "main.c" 
                            "ui_dynamic.c" 
                            "scan_logic.c"
                            "ui_logic.c" 
                            ${SRC_UI}
                       INCLUDE_DIRS "." "ui")