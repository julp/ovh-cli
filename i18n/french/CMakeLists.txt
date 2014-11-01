get_filename_component(LANG ${CMAKE_CURRENT_SOURCE_DIR} NAME)
gettext_process_po_files(${LANG} ALL INSTALL_DESTINATION share/locale/ PO_FILES ovh-cli.po)
