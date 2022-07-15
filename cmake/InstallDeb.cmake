# DEB Stuff
set(CPACK_DEB_COMPONENT_INSTALL ON)

set(CPACK_DEBIAN_runtime_PACKAGE_SECTION graphics)
set(CPACK_DEBIAN_frontends_PACKAGE_SECTION graphics)
set(CPACK_DEBIAN_tools_PACKAGE_SECTION graphics)

set(CPACK_DEBIAN_documentation_PACKAGE_SECTION doc)
set(CPACK_DEBIAN_documentation_PACKAGE_PRIORITY extra)

set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_runtime_PACKAGE_DEPENDS}, ${CPACK_DEBIAN_development_PACKAGE_DEPENDS}")
