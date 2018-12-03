find_ups_product( lardataobj v1_17_00 )

# include Lariat Geo
add_subdirectory(LariatGeo)

# include Lariat Data PRoducts
add_subdirectory(LariatDataProducts)

# add in VST Calibration
add_subdirectory(VSTCalibration)

# add in Redis stuff
# don't try to build if not set up
if( DEFINED ENV{HIREDIS_INC} )
  add_subdirectory(Redis)
endif()

# add header-decoder stuff
# don't try to build if not set up
if( DEFINED ENV{SBNDDAQ_DATATYPES_INC} )
  add_subdirectory(Decoder)
endif()

# filter modules for running online monitoring
add_subdirectory(OnlineFilters)

link_directories($ENV{FFTW_LIBRARY})

art_dictionary(DICTIONARY_LIBRARIES daqAnalysis_VST)

simple_plugin( VSTChannelMap service
  ${LARDATAOBJ}
  lardataobj_RawData
  ${ART_UTILITIES}
  ${FHICLCPP}
  ${ART_FRAMEWORK_CORE}
  ${ROOT_BASIC_LIB_LIST}
  ${ART_FRAMEWORK_SERVICES_OPTIONAL_TFILESERVICE_SERVICE}
)
  
cet_make_library( LIBRARY_NAME daqAnalysis_MODE
	SOURCE
		Mode.cc
)

cet_make_library( LIBRARY_NAME daqAnalysis_VST
	SOURCE  Analysis.cc
		FFT.cc
		Noise.cc
		PeakFinder.cc
		ChannelData.cc
    Purity.cc
	LIBRARIES
		daqAnalysis_MODE
  		sbndcode_VSTAnalysis_VSTChannelMap_service
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
                           larcorealg_Geometry
                           lardataobj_Simulation
                           lardata_Utilities
                           lardataobj_RawData
                           lardata_DetectorInfoServices_DetectorClocksServiceStandard_service
                           sbndcode_Utilities_SignalShapingServiceSBND_service
                           sbndcode_Utilities_SignalShapingServiceVST_service
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

simple_plugin( VSTAnalysis module
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


Simple_plugin( RawHitFinderVST module
                        lardata_ArtDataHelper
                        larreco_HitFinder
                        #larsim_MCCheater_BackTracker_service
                        larreco_RecoAlg
                        lardataobj_RecoBase
                        lardataobj_MCBase
                        larcorealg_Geometry
                        larcore_Geometry_Geometry_service
                        lardata_Utilities
                        larsim_Simulation nutools_ParticleNavigation lardataobj_Simulation
                        nusimdata_SimulationBase
                        ${ART_FRAMEWORK_CORE}
                        ${ART_FRAMEWORK_PRINCIPAL}
                        ${ART_FRAMEWORK_SERVICES_REGISTRY}
                        ${ART_FRAMEWORK_SERVICES_BASIC}
                        ${ART_FRAMEWORK_SERVICES_OPTIONAL}
                        ${ART_FRAMEWORK_SERVICES_OPTIONAL_TFILESERVICE_SERVICE}
                        art_Persistency_Common
                        art_Utilities
                        canvas
                        ${MF_MESSAGELOGGER}
                        ${MF_UTILITIES}
                        ${FHICLCPP}
                        cetlib cetlib_except
                        ${ROOT_BASIC_LIB_LIST}
                        lardataobj_RawData
                        lardataobj_RecoBase
                        larcorealg_Geometry
                        larcore_Geometry_Geometry_service
                        art_Persistency_Provenance
                        art_Utilities
                        ${Boost_SYSTEM_LIBRARY}

)


install_headers()
install_fhicl()
install_source()

# install fcl
add_subdirectory(fcl)


