# setup external non-ups libraries
include_directories($ENV{HIREDIS_INC})
link_directories($ENV{HIREDIS_LIB})

cet_make_library( LIBRARY_NAME daqAnalysis_Redis
	SOURCE
		Redis.cc
		RedisData.cc
	LIBRARIES
		daqAnalysis_VST
		daqAnalysis_MODE
		sbndcode_VSTAnalysis_VSTChannelMap_service
		hiredis
		sbnddaq-datatypes_Overlays
		sbnddaq-datatypes_NevisTPC
		${LARDATAOBJ} 
		lardataobj_RawData
		lardata_Utilities
        	${ART_FRAMEWORK_IO_SOURCES}
       		${ART_FRAMEWORK_PRINCIPAL}
       		canvas
	        ${ART_FRAMEWORK_SERVICES_REGISTRY}
		${ART_FRAMEWORK_SERVICES_OPTIONAL_TFILESERVICE_SERVICE}
		art_Framework_Services_Optional_TFileService_service
		art_Framework_Services_FileServiceInterfaces
		${ART_UTILITIES}
		${FHICLCPP}
		${ART_FRAMEWORK_CORE}
		${ART_FRAMEWORK_PRINCIPAL}
		${ROOT_BASIC_LIB_LIST} 
		fftw3
			   larcore_Geometry_Geometry_service
                           lardataobj_Simulation
                           lardata_Utilities
                           lardataobj_RawData
                           lardata_DetectorInfoServices_DetectorClocksServiceStandard_service
                           sbndcode_Utilities_SignalShapingServiceSBND_service
                           nutools_RandomUtils_NuRandomService_service
                           ${ART_FRAMEWORK_CORE}
                           ${ART_FRAMEWORK_PRINCIPAL}
                           ${ART_FRAMEWORK_SERVICES_OPTIONAL_TFILESERVICE_SERVICE}
                           ${ART_FRAMEWORK_SERVICES_OPTIONAL_RANDOMNUMBERGENERATOR_SERVICE}
                           canvas
                           ${MF_MESSAGELOGGER}
                           ${MF_UTILITIES}
                           ${FHICLCPP}
                           cetlib cetlib_except
                           ${CLHEP}
                           ${ROOT_BASIC_LIB_LIST}
)

simple_plugin( OnlineAnalysis module
  daqAnalysis_Redis
  daqAnalysis_VST
  sbndcode_VSTAnalysis_VSTChannelMap_service
  ${LARDATAOBJ}
  lardataobj_RawData
  ${ART_UTILITIES}
  ${FHICLCPP}
  ${ART_FRAMEWORK_CORE}
  ${ROOT_BASIC_LIB_LIST}
  ${ART_FRAMEWORK_SERVICES_OPTIONAL_TFILESERVICE_SERVICE}
)

install_headers()
install_fhicl()
install_source()

