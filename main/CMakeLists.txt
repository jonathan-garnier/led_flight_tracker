idf_component_register(SRCS "main.c"
                            "config_storage.c"
                            "wifi_manager.c"
                            "web_server.c"
                            "flight_api.c"
                            "route_api.c"
                            "route_cache.c"
                            "button.c"
                            "display.cpp"
                       INCLUDE_DIRS ".")

set_source_files_properties(display.cpp PROPERTIES COMPILE_FLAGS "-Wno-reorder")
