option(USE_SHARED_POLARSSL_LIBRARY "Build PolarSSL as a shared library." OFF)

set(src
     aes.c
     arc4.c
     asn1parse.c
     asn1write.c
     base64.c
     bignum.c
     blowfish.c
     camellia.c
     certs.c
     cipher.c
     cipher_wrap.c
     ctr_drbg.c
     debug.c
     des.c
     dhm.c
     entropy.c
     entropy_poll.c
     error.c
     gcm.c
     havege.c
     md.c
     md_wrap.c
     md2.c
     md4.c
     md5.c
     net.c
     padlock.c
     pbkdf2.c
     pem.c
     pkcs5.c
     pkcs11.c
     pkcs12.c
     rsa.c
     sha1.c
     sha2.c
     sha4.c
     ssl_cache.c
     ssl_cli.c 
     ssl_srv.c 
     ssl_tls.c
     timing.c
     version.c
     x509parse.c
     x509write.c
     xtea.c
)

if(WIN32)
set(src
	${src}
	libpolarssl.def
)

set(libs ws2_32)
endif(WIN32)

if(NOT USE_SHARED_POLARSSL_LIBRARY)

add_library(polarssl STATIC ${src})

else(NOT USE_SHARED_POLARSSL_LIBRARY)

add_library(polarssl SHARED ${src})
set_target_properties(polarssl PROPERTIES VERSION 1.2.10 SOVERSION 3)

target_link_libraries(polarssl ${libs})

endif(NOT USE_SHARED_POLARSSL_LIBRARY)

install(TARGETS polarssl
	DESTINATION ${LIB_INSTALL_DIR}
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

if(USE_SHARED_POLARSSL_LIBRARY)
	if(MSVC)
		if(CMAKE_BUILD_TYPE STREQUAL "Debug")
			install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Debug/polarssl.pdb
				DESTINATION ${LIB_INSTALL_DIR}
				PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
			)
		endif(CMAKE_BUILD_TYPE STREQUAL "Debug")
	endif(MSVC)
endif(USE_SHARED_POLARSSL_LIBRARY)
