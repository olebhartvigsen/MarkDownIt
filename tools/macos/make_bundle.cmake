# Assembles MarkDownIt.app from the built binary.
#
# Run by the macOS CI job with:
#   cmake -P tools/macos/make_bundle.cmake
#
# The bundle layout is the standard one:
#   MarkDownIt.app/Contents/MacOS/MarkDownIt      the binary
#   MarkDownIt.app/Contents/Resources/app.icns    the icon
#   MarkDownIt.app/Contents/Info.plist            the metadata
#
# The plist declares the markdown document types, so a .md file can be
# opened with MarkDownIt from the Finder.

# The build folder. A "cmake -P" run has no build directory of its own,
# so the folder is passed in:
#
#   cmake -DBUNDLE_BUILD_DIR=build-mac -P tools/macos/make_bundle.cmake
#
# The working directory is the fallback, for a run from inside the build
# folder.
if(NOT DEFINED BUNDLE_BUILD_DIR OR BUNDLE_BUILD_DIR STREQUAL "")
    set(BUNDLE_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# The source folder, for the icon.
if(NOT DEFINED BUNDLE_SOURCE_DIR OR BUNDLE_SOURCE_DIR STREQUAL "")
    get_filename_component(BUNDLE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(BUNDLE_DIR "${BUNDLE_BUILD_DIR}/MarkDownIt.app")
set(CONTENTS_DIR "${BUNDLE_DIR}/Contents")
set(MACOS_DIR "${CONTENTS_DIR}/MacOS")
set(RESOURCES_DIR "${CONTENTS_DIR}/Resources")

# The binary. The build target is named MarkDownItMac but writes a file
# called MarkDownIt.
if(NOT EXISTS "${BUNDLE_BUILD_DIR}/MarkDownIt")
    message(FATAL_ERROR
        "MarkDownIt binary not found at ${BUNDLE_BUILD_DIR}/MarkDownIt. "
        "Build the MarkDownItMac target first.")
endif()

file(REMOVE_RECURSE "${BUNDLE_DIR}")
file(MAKE_DIRECTORY "${MACOS_DIR}")
file(MAKE_DIRECTORY "${RESOURCES_DIR}")

file(COPY "${BUNDLE_BUILD_DIR}/MarkDownIt"
     DESTINATION "${MACOS_DIR}"
     FILE_PERMISSIONS
         OWNER_READ OWNER_WRITE OWNER_EXECUTE
         GROUP_READ GROUP_EXECUTE
         WORLD_READ WORLD_EXECUTE)

# The icon, when the build produced one. The bundle still works without
# it, it just shows the generic application icon.
if(EXISTS "${BUNDLE_SOURCE_DIR}/tools/macos/app.icns")
    file(COPY "${BUNDLE_SOURCE_DIR}/tools/macos/app.icns"
         DESTINATION "${RESOURCES_DIR}")
endif()

set(PLIST "${CONTENTS_DIR}/Info.plist")
file(WRITE "${PLIST}" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>MarkDownIt</string>
    <key>CFBundleIdentifier</key>
    <string>dk.markdownit.MarkDownIt</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>MarkDownIt</string>
    <key>CFBundleDisplayName</key>
    <string>MarkDownIt</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>Markdown Document</string>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
            <key>LSHandlerRank</key>
            <string>Alternate</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>net.daringfireball.markdown</string>
                <string>public.plain-text</string>
            </array>
        </dict>
    </array>
    <key>UTExportedTypeDeclarations</key>
    <array>
        <dict>
            <key>UTTypeIdentifier</key>
            <string>net.daringfireball.markdown</string>
            <key>UTTypeDescription</key>
            <string>Markdown Document</string>
            <key>UTTypeConformsTo</key>
            <array>
                <string>public.plain-text</string>
            </array>
            <key>UTTypeTagSpecification</key>
            <dict>
                <key>public.filename-extension</key>
                <array>
                    <string>md</string>
                    <string>markdown</string>
                    <string>mdown</string>
                    <string>mkd</string>
                </array>
                <key>public.mime-type</key>
                <array>
                    <string>text/markdown</string>
                </array>
            </dict>
        </dict>
    </array>
</dict>
</plist>
")

message(STATUS "Bundle written to ${BUNDLE_DIR}")
