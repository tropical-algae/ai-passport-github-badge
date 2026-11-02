idf_component_register(
    SRCS "badge_app.c" "badge_model.c" "badge_input.c" "badge_idle.c" "badge_storage.c"
         "badge_backup.c" "badge_ui.c" "badge_ui_state.c" "badge_network.c" "badge_portal.c"
         "badge_sync.c" "badge_sync_parse.c" "badge_sync_logic.c"
         "../assets/fonts/github-badge/badge_font_8.c"
         "../assets/fonts/github-badge/badge_font_10.c"
         "../assets/fonts/github-badge/badge_font_14.c"
         "../assets/fonts/github-badge/badge_font_22.c"
         "../assets/fonts/github-badge/badge_font_cjk_10.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi nvs_flash spiffs
             esp_http_client esp_http_server esp-tls json lwip
    EMBED_TXTFILES "badge_portal.html"
)
