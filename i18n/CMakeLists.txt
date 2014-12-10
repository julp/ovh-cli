add_subdirectory(fr)

# file(GLOB PO_FILES *.po)
# foreach(PO_FILE ${PO_FILES})
#     get_filename_component(LANG ${PO_FILE} NAME_WE)
#     # INSTALL_DESTINATION share/locale/
#     gettext_process_po_files(${LANG} ALL INSTALL_DESTINATION share/locale/ PO_FILES ${PO_FILE})
# endforeach(PO_FILE)
