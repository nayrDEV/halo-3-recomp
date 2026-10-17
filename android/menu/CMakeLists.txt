add_library(slintmenu SHARED
    main.cpp
    android_bridge.cpp
    iso.cpp
    ${H3_ROOT}/H3Recomp/config/settings.cpp
)

target_include_directories(slintmenu PRIVATE
    ${H3_ROOT}/H3Recomp
)

slint_target_sources(slintmenu ui/main.slint)
target_link_libraries(slintmenu PRIVATE Slint::Slint)
set_target_properties(slintmenu PROPERTIES OUTPUT_NAME slintmenu)
