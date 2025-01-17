set(ARTIC_MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/color.art
    ${CMAKE_CURRENT_SOURCE_DIR}/core/common.art
    ${CMAKE_CURRENT_SOURCE_DIR}/core/cpu_common.art
    ${CMAKE_CURRENT_SOURCE_DIR}/core/interval.art
    ${CMAKE_CURRENT_SOURCE_DIR}/core/matrix.art
    ${CMAKE_CURRENT_SOURCE_DIR}/core/print.art
    ${CMAKE_CURRENT_SOURCE_DIR}/core/random.art
    ${CMAKE_CURRENT_SOURCE_DIR}/core/sort.art
    ${CMAKE_CURRENT_SOURCE_DIR}/core/vector.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/aov.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/bsdf.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/camera.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/data.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/device.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/driver.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/entity.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/image.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/light.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/mapping_cpu.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/mapping_gpu.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/material.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/pipeline.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/renderer.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/scene.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/settings.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/shape.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/streams.art
    ${CMAKE_CURRENT_SOURCE_DIR}/render/texture.art
    ${CMAKE_CURRENT_SOURCE_DIR}/traversal/intersection.art
    ${CMAKE_CURRENT_SOURCE_DIR}/traversal/mapping_cpu.art
    ${CMAKE_CURRENT_SOURCE_DIR}/traversal/mapping_gpu.art
    ${CMAKE_CURRENT_SOURCE_DIR}/traversal/stack.art
)

set(ARTIC_EXTRA_SRC
    ${ARTIC_MAIN_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/aotracer.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/bsdf.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/camera.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/cdf.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/debugtracer.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/emitter.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/fresnel.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/klems.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/light.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/microfacet.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/pathtracer.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/pixel_sampler.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/principled.art
    ${CMAKE_CURRENT_SOURCE_DIR}/impl/texture.art
)

set(ARTIC_MAIN_SRC ${ARTIC_MAIN_SRC} PARENT_SCOPE)
set(ARTIC_EXTRA_SRC ${ARTIC_EXTRA_SRC} PARENT_SCOPE)

anydsl_runtime_wrap(DISCARD_TMP_OBJS
    NAME "interface"
    FRONTEND "artic"
    FILES ${ARTIC_MAIN_SRC} dummy_main.art
    INTERFACE ${CMAKE_BINARY_DIR}/generated_interface)
add_custom_target(artic_c_interface ALL DEPENDS ${CMAKE_BINARY_DIR}/generated_interface.h)
