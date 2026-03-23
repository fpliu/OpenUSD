set(PXR_PREFIX pxr/imaging)
set(PXR_PACKAGE hdMtlx)

set(optionalLibs "")
set(optionalPublicClasses "")
if(PXR_ENABLE_MATERIALX_GENSHADER2)
    list(APPEND optionalLibs MaterialXGenShader2)
    list(APPEND optionalPublicClasses hdMtlxShaderSource)
endif()

pxr_library(hdMtlx
    LIBRARIES
        gf
        hd
        sdf
        sdr
        tf
        trace
        usdMtlx
        vt
        MaterialXCore
        MaterialXFormat
        ${optionalLibs}

    PUBLIC_CLASSES
        hdMtlx
        debugCodes
        tokens
        ${optionalPublicClasses}

    PUBLIC_HEADERS
        api.h
        combinedMtlxVersion.h
)
