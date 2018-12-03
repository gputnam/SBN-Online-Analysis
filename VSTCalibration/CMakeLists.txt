simple_plugin( CalWireVST module
                        lardata_Utilities
                        lardataobj_RawData
                        lardataobj_RecoBase
                        sbndcode_Utilities_SignalShapingServiceVST_service
                        sbndcode_Utilities_SignalShapingServiceSBND_service
                        ${ART_FRAMEWORK_CORE}
                        ${ART_FRAMEWORK_PRINCIPAL}
                        ${ART_FRAMEWORK_SERVICES_REGISTRY}
                        ${ART_FRAMEWORK_SERVICES_OPTIONAL}
                        ${ART_FRAMEWORK_SERVICES_OPTIONAL_TFILESERVICE_SERVICE}
                        canvas
                        ${MF_MESSAGELOGGER}
                        ${MF_UTILITIES}
                        ${FHICLCPP}
                        cetlib cetlib_except
                        ${ROOT_GEOM}
                        ${ROOT_XMLIO}
                        ${ROOT_GDML}
                        ${ROOT_BASIC_LIB_LIST}
)

install_headers()
install_fhicl()
install_source()

