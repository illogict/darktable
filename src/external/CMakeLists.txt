set(BUILD_TESTING_SAVE "${BUILD_TESTING}")

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(rawspeed)

#
# the rawspeed part is a bit of a hack:
# the static linking didn't work since it was pulling -lstdc++ and -lm into linker flags.
# so we do a custom dependency and pretend an imported librawsped.a so no other -l are
# appended.
#
add_library(rawspeed_static_dt STATIC IMPORTED GLOBAL)
add_dependencies(rawspeed_static_dt rawspeed_static rawspeed)
set_target_properties(rawspeed_static_dt
  PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/rawspeed/RawSpeed/librawspeed_static.a
    INTERFACE_LINK_LIBRARIES $<TARGET_PROPERTY:rawspeed_static,INTERFACE_LINK_LIBRARIES>
    POSITION_INDEPENDENT_CODE True
)

set(BUILD_TESTING "${BUILD_TESTING_SAVE}" CACHE BOOL "" FORCE)
