idf_component_register(SRCS "http_file_ops.c" "ir_codec.c" "ir_http_server.c" "wifi.c" "main.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "cert/servercert.pem"
                                   "cert/prvtkey.pem"
					)

spiffs_create_partition_image(html ./html FLASH_IN_PROJECT)