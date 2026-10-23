idf_component_register(
    SRCS "eye_canvas.c" "eye_renderer.c" "eye_animator.c"
         "styles/style_sphere.c"
         "blink/blink_effects.c"
         "gaze/gaze_idle.c" "gaze/gaze_center.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_hw_support esp_timer heap log freertos
)
