/*
 *  SSLv3/TLSv1 client-side functions
 *
 *  Copyright (C) 2006-2014, Brainspark B.V.
 *
 *  This file is part of PolarSSL (http://www.polarssl.org)
 *  Lead Maintainer: Paul Bakker <polarssl_maintainer at polarssl.org>
 *
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if !defined(POLARSSL_CONFIG_FILE)
#include "polarssl/config.h"
#else
#include POLARSSL_CONFIG_FILE
#endif

#if defined(POLARSSL_SSL_CLI_C)

#include "polarssl/debug.h"
#include "polarssl/ssl.h"

#if defined(POLARSSL_PLATFORM_C)
#include "polarssl/platform.h"
#else
#define polarssl_malloc     malloc
#define polarssl_free       free
#endif

#include <stdlib.h>
#include <stdio.h>

#if defined(_MSC_VER) && !defined(EFIX64) && !defined(EFI32)
#include <basetsd.h>
typedef UINT32 uint32_t;
#else
#include <inttypes.h>
#endif

#if defined(POLARSSL_HAVE_TIME)
#include <time.h>
#endif

#if defined(POLARSSL_SSL_SESSION_TICKETS)
/* Implementation that should never be optimized out by the compiler */
static void polarssl_zeroize( void *v, size_t n ) {
    volatile unsigned char *p = v; while( n-- ) *p++ = 0;
}
#endif

#if defined(POLARSSL_SSL_SERVER_NAME_INDICATION)
static void ssl_write_hostname_ext( ssl_context *ssl,
                                    unsigned char *buf,
                                    size_t *olen )
{
    unsigned char *p = buf;

    *olen = 0;

    if( ssl->hostname == NULL )
        return;

    SSL_DEBUG_MSG( 3, ( "client hello, adding server name extension: %s",
                   ssl->hostname ) );

    /*
     * struct {
     *     NameType name_type;
     *     select (name_type) {
     *         case host_name: HostName;
     *     } name;
     * } ServerName;
     *
     * enum {
     *     host_name(0), (255)
     * } NameType;
     *
     * opaque HostName<1..2^16-1>;
     *
     * struct {
     *     ServerName server_name_list<1..2^16-1>
     * } ServerNameList;
     */
    *p++ = (unsigned char)( ( TLS_EXT_SERVERNAME >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_SERVERNAME      ) & 0xFF );

    *p++ = (unsigned char)( ( (ssl->hostname_len + 5) >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( (ssl->hostname_len + 5)      ) & 0xFF );

    *p++ = (unsigned char)( ( (ssl->hostname_len + 3) >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( (ssl->hostname_len + 3)      ) & 0xFF );

    *p++ = (unsigned char)( ( TLS_EXT_SERVERNAME_HOSTNAME ) & 0xFF );
    *p++ = (unsigned char)( ( ssl->hostname_len >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( ssl->hostname_len      ) & 0xFF );

    memcpy( p, ssl->hostname, ssl->hostname_len );

    *olen = ssl->hostname_len + 9;
}
#endif /* POLARSSL_SSL_SERVER_NAME_INDICATION */

static void ssl_write_renegotiation_ext( ssl_context *ssl,
                                         unsigned char *buf,
                                         size_t *olen )
{
    unsigned char *p = buf;

    *olen = 0;

    if( ssl->renegotiation != SSL_RENEGOTIATION )
        return;

    SSL_DEBUG_MSG( 3, ( "client hello, adding renegotiation extension" ) );

    /*
     * Secure renegotiation
     */
    *p++ = (unsigned char)( ( TLS_EXT_RENEGOTIATION_INFO >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_RENEGOTIATION_INFO      ) & 0xFF );

    *p++ = 0x00;
    *p++ = ( ssl->verify_data_len + 1 ) & 0xFF;
    *p++ = ssl->verify_data_len & 0xFF;

    memcpy( p, ssl->own_verify_data, ssl->verify_data_len );

    *olen = 5 + ssl->verify_data_len;
}

#if defined(POLARSSL_SSL_PROTO_TLS1_2)
static void ssl_write_signature_algorithms_ext( ssl_context *ssl,
                                                unsigned char *buf,
                                                size_t *olen )
{
    unsigned char *p = buf;
    size_t sig_alg_len = 0;
#if defined(POLARSSL_RSA_C) || defined(POLARSSL_ECDSA_C)
    unsigned char *sig_alg_list = buf + 6;
#endif

    *olen = 0;

    if( ssl->max_minor_ver != SSL_MINOR_VERSION_3 )
        return;

    SSL_DEBUG_MSG( 3, ( "client hello, adding signature_algorithms extension" ) );

    /*
     * Prepare signature_algorithms extension (TLS 1.2)
     */
#if defined(POLARSSL_RSA_C)
#if defined(POLARSSL_SHA512_C)
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA512;
    sig_alg_list[sig_alg_len++] = SSL_SIG_RSA;
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA384;
    sig_alg_list[sig_alg_len++] = SSL_SIG_RSA;
#endif
#if defined(POLARSSL_SHA256_C)
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA256;
    sig_alg_list[sig_alg_len++] = SSL_SIG_RSA;
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA224;
    sig_alg_list[sig_alg_len++] = SSL_SIG_RSA;
#endif
#if defined(POLARSSL_SHA1_C)
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA1;
    sig_alg_list[sig_alg_len++] = SSL_SIG_RSA;
#endif
#if defined(POLARSSL_MD5_C)
    sig_alg_list[sig_alg_len++] = SSL_HASH_MD5;
    sig_alg_list[sig_alg_len++] = SSL_SIG_RSA;
#endif
#endif /* POLARSSL_RSA_C */
#if defined(POLARSSL_ECDSA_C)
#if defined(POLARSSL_SHA512_C)
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA512;
    sig_alg_list[sig_alg_len++] = SSL_SIG_ECDSA;
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA384;
    sig_alg_list[sig_alg_len++] = SSL_SIG_ECDSA;
#endif
#if defined(POLARSSL_SHA256_C)
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA256;
    sig_alg_list[sig_alg_len++] = SSL_SIG_ECDSA;
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA224;
    sig_alg_list[sig_alg_len++] = SSL_SIG_ECDSA;
#endif
#if defined(POLARSSL_SHA1_C)
    sig_alg_list[sig_alg_len++] = SSL_HASH_SHA1;
    sig_alg_list[sig_alg_len++] = SSL_SIG_ECDSA;
#endif
#if defined(POLARSSL_MD5_C)
    sig_alg_list[sig_alg_len++] = SSL_HASH_MD5;
    sig_alg_list[sig_alg_len++] = SSL_SIG_ECDSA;
#endif
#endif /* POLARSSL_ECDSA_C */

    /*
     * enum {
     *     none(0), md5(1), sha1(2), sha224(3), sha256(4), sha384(5),
     *     sha512(6), (255)
     * } HashAlgorithm;
     *
     * enum { anonymous(0), rsa(1), dsa(2), ecdsa(3), (255) }
     *   SignatureAlgorithm;
     *
     * struct {
     *     HashAlgorithm hash;
     *     SignatureAlgorithm signature;
     * } SignatureAndHashAlgorithm;
     *
     * SignatureAndHashAlgorithm
     *   supported_signature_algorithms<2..2^16-2>;
     */
    *p++ = (unsigned char)( ( TLS_EXT_SIG_ALG >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_SIG_ALG      ) & 0xFF );

    *p++ = (unsigned char)( ( ( sig_alg_len + 2 ) >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( ( sig_alg_len + 2 )      ) & 0xFF );

    *p++ = (unsigned char)( ( sig_alg_len >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( sig_alg_len      ) & 0xFF );

    *olen = 6 + sig_alg_len;
}
#endif /* POLARSSL_SSL_PROTO_TLS1_2 */

#if defined(POLARSSL_ECDH_C) || defined(POLARSSL_ECDSA_C)
static void ssl_write_supported_elliptic_curves_ext( ssl_context *ssl,
                                                     unsigned char *buf,
                                                     size_t *olen )
{
    unsigned char *p = buf;
    unsigned char *elliptic_curve_list = p + 6;
    size_t elliptic_curve_len = 0;
    const ecp_curve_info *info;
#if defined(POLARSSL_SSL_SET_CURVES)
    const ecp_group_id *grp_id;
#else
    ((void) ssl);
#endif

    *olen = 0;

    SSL_DEBUG_MSG( 3, ( "client hello, adding supported_elliptic_curves extension" ) );

#if defined(POLARSSL_SSL_SET_CURVES)
    for( grp_id = ssl->curve_list; *grp_id != POLARSSL_ECP_DP_NONE; grp_id++ )
    {
        info = ecp_curve_info_from_grp_id( *grp_id );
#else
    for( info = ecp_curve_list(); info->grp_id != POLARSSL_ECP_DP_NONE; info++ )
    {
#endif

        elliptic_curve_list[elliptic_curve_len++] = info->tls_id >> 8;
        elliptic_curve_list[elliptic_curve_len++] = info->tls_id & 0xFF;
    }

    if( elliptic_curve_len == 0 )
        return;

    *p++ = (unsigned char)( ( TLS_EXT_SUPPORTED_ELLIPTIC_CURVES >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_SUPPORTED_ELLIPTIC_CURVES      ) & 0xFF );

    *p++ = (unsigned char)( ( ( elliptic_curve_len + 2 ) >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( ( elliptic_curve_len + 2 )      ) & 0xFF );

    *p++ = (unsigned char)( ( ( elliptic_curve_len     ) >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( ( elliptic_curve_len     )      ) & 0xFF );

    *olen = 6 + elliptic_curve_len;
}

static void ssl_write_supported_point_formats_ext( ssl_context *ssl,
                                                   unsigned char *buf,
                                                   size_t *olen )
{
    unsigned char *p = buf;
    ((void) ssl);

    *olen = 0;

    SSL_DEBUG_MSG( 3, ( "client hello, adding supported_point_formats extension" ) );

    *p++ = (unsigned char)( ( TLS_EXT_SUPPORTED_POINT_FORMATS >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_SUPPORTED_POINT_FORMATS      ) & 0xFF );

    *p++ = 0x00;
    *p++ = 2;

    *p++ = 1;
    *p++ = POLARSSL_ECP_PF_UNCOMPRESSED;

    *olen = 6;
}
#endif /* POLARSSL_ECDH_C || POLARSSL_ECDSA_C */

#if defined(POLARSSL_SSL_MAX_FRAGMENT_LENGTH)
static void ssl_write_max_fragment_length_ext( ssl_context *ssl,
                                               unsigned char *buf,
                                               size_t *olen )
{
    unsigned char *p = buf;

    if( ssl->mfl_code == SSL_MAX_FRAG_LEN_NONE ) {
        *olen = 0;
        return;
    }

    SSL_DEBUG_MSG( 3, ( "client hello, adding max_fragment_length extension" ) );

    *p++ = (unsigned char)( ( TLS_EXT_MAX_FRAGMENT_LENGTH >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_MAX_FRAGMENT_LENGTH      ) & 0xFF );

    *p++ = 0x00;
    *p++ = 1;

    *p++ = ssl->mfl_code;

    *olen = 5;
}
#endif /* POLARSSL_SSL_MAX_FRAGMENT_LENGTH */

#if defined(POLARSSL_SSL_TRUNCATED_HMAC)
static void ssl_write_truncated_hmac_ext( ssl_context *ssl,
                                          unsigned char *buf, size_t *olen )
{
    unsigned char *p = buf;

    if( ssl->trunc_hmac == SSL_TRUNC_HMAC_DISABLED )
    {
        *olen = 0;
        return;
    }

    SSL_DEBUG_MSG( 3, ( "client hello, adding truncated_hmac extension" ) );

    *p++ = (unsigned char)( ( TLS_EXT_TRUNCATED_HMAC >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_TRUNCATED_HMAC      ) & 0xFF );

    *p++ = 0x00;
    *p++ = 0x00;

    *olen = 4;
}
#endif /* POLARSSL_SSL_TRUNCATED_HMAC */

#if defined(POLARSSL_SSL_SESSION_TICKETS)
static void ssl_write_session_ticket_ext( ssl_context *ssl,
                                          unsigned char *buf, size_t *olen )
{
    unsigned char *p = buf;
    size_t tlen = ssl->session_negotiate->ticket_len;

    if( ssl->session_tickets == SSL_SESSION_TICKETS_DISABLED )
    {
        *olen = 0;
        return;
    }

    SSL_DEBUG_MSG( 3, ( "client hello, adding session ticket extension" ) );

    *p++ = (unsigned char)( ( TLS_EXT_SESSION_TICKET >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_SESSION_TICKET      ) & 0xFF );

    *p++ = (unsigned char)( ( tlen >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( tlen      ) & 0xFF );

    *olen = 4;

    if( ssl->session_negotiate->ticket == NULL ||
        ssl->session_negotiate->ticket_len == 0 )
    {
        return;
    }

    SSL_DEBUG_MSG( 3, ( "sending session ticket of length %d", tlen ) );

    memcpy( p, ssl->session_negotiate->ticket, tlen );

    *olen += tlen;
}
#endif /* POLARSSL_SSL_SESSION_TICKETS */

#if defined(POLARSSL_SSL_ALPN)
static void ssl_write_alpn_ext( ssl_context *ssl,
                                unsigned char *buf, size_t *olen )
{
    unsigned char *p = buf;
    const char **cur;

    if( ssl->alpn_list == NULL )
    {
        *olen = 0;
        return;
    }

    SSL_DEBUG_MSG( 3, ( "client hello, adding alpn extension" ) );

    *p++ = (unsigned char)( ( TLS_EXT_ALPN >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_ALPN      ) & 0xFF );

    /*
     * opaque ProtocolName<1..2^8-1>;
     *
     * struct {
     *     ProtocolName protocol_name_list<2..2^16-1>
     * } ProtocolNameList;
     */

    /* Skip writing extension and list length for now */
    p += 4;

    for( cur = ssl->alpn_list; *cur != NULL; cur++ )
    {
        *p = (unsigned char)( strlen( *cur ) & 0xFF );
        memcpy( p + 1, *cur, *p );
        p += 1 + *p;
    }

    *olen = p - buf;

    /* List length = olen - 2 (ext_type) - 2 (ext_len) - 2 (list_len) */
    buf[4] = (unsigned char)( ( ( *olen - 6 ) >> 8 ) & 0xFF );
    buf[5] = (unsigned char)( ( ( *olen - 6 )      ) & 0xFF );

    /* Extension length = olen - 2 (ext_type) - 2 (ext_len) */
    buf[2] = (unsigned char)( ( ( *olen - 4 ) >> 8 ) & 0xFF );
    buf[3] = (unsigned char)( ( ( *olen - 4 )      ) & 0xFF );
}
#endif /* POLARSSL_SSL_ALPN */

#if defined (POLARSSL_SSL_PROTO_DTLS)
static void ssl_write_use_srtp_ext( ssl_context *ssl,
                                unsigned char *buf, size_t *olen )
{
    unsigned char *p = buf;
    size_t protection_profiles_index = 0;

    *olen = 0;

    if( (ssl->dtls_srtp_profiles_list == NULL)  || (ssl->dtls_srtp_profiles_list_len == 0) )
    {
        return;
    }

    SSL_DEBUG_MSG( 3, ( "client hello, adding use_srtp extension" ) );

    *p++ = (unsigned char)( ( TLS_EXT_USE_SRTP >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( TLS_EXT_USE_SRTP      ) & 0xFF );

    /* RFC5764 section 4.1.1
     * uint8 SRTPProtectionProfile[2];
     * 
     * struct {
     *   SRTPProtectionProfiles SRTPProtectionProfiles;
     *   opaque srtp_mki<0..255>;
     * } UseSRTPData;

     * SRTPProtectionProfile SRTPProtectionProfiles<2..2^16-1>;
     *
     * Note: srtp_mki is not supported
     */

    /* Extension length = 2bytes for profiles lenght, ssl->dtls_srtp_profiles_list_len*2 (each profile is 2 bytes length ) + 1 byte for the non implemented srtp_mki vector length (always 0) */
    *p++ = (unsigned char)( ( ( 2 + 2*(ssl->dtls_srtp_profiles_list_len) + 1 ) >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( ( 2 + 2*(ssl->dtls_srtp_profiles_list_len) + 1 )      ) & 0xFF );


    /* protection profile length: 2*(ssl->dtls_srtp_profiles_list_len) */
    *p++ = (unsigned char)( ( ( 2*(ssl->dtls_srtp_profiles_list_len) ) >> 8 ) & 0xFF );
    *p++ = (unsigned char)( ( 2*(ssl->dtls_srtp_profiles_list_len) ) & 0xFF );

    for( protection_profiles_index=0; protection_profiles_index < ssl->dtls_srtp_profiles_list_len; protection_profiles_index++ )
    {
        switch (ssl->dtls_srtp_profiles_list[protection_profiles_index]) {
            case SRTP_AES128_CM_HMAC_SHA1_80:
                *p++ = ( ( ( SRTP_AES128_CM_HMAC_SHA1_80_IANA_VALUE ) >> 8 ) & 0xFF);
                *p++ = ( ( SRTP_AES128_CM_HMAC_SHA1_80_IANA_VALUE ) & 0xFF);
                break;
            case SRTP_AES128_CM_HMAC_SHA1_32:
                *p++ = ( ( ( SRTP_AES128_CM_HMAC_SHA1_32_IANA_VALUE ) >> 8 ) & 0xFF);
                *p++ = ( ( SRTP_AES128_CM_HMAC_SHA1_32_IANA_VALUE ) & 0xFF);
                break;
            case SRTP_NULL_HMAC_SHA1_80:
                *p++ = ( ( ( SRTP_NULL_HMAC_SHA1_80_IANA_VALUE ) >> 8 ) & 0xFF);
                *p++ = ( ( SRTP_NULL_HMAC_SHA1_80_IANA_VALUE ) & 0xFF);
                break;
            case SRTP_NULL_HMAC_SHA1_32:
                *p++ = ( ( ( SRTP_NULL_HMAC_SHA1_32_IANA_VALUE ) >> 8 ) & 0xFF);
                *p++ = ( ( SRTP_NULL_HMAC_SHA1_32_IANA_VALUE ) & 0xFF);
                break;
            default:
                /* Note: we shall never arrive here as protection profiles is checked by ssl_set_dtls_srtp_protection_profiles function */
                SSL_DEBUG_MSG( 1, ( "client hello, ignore illegal DTLS-SRTP protection profile %d",  ssl->dtls_srtp_profiles_list[protection_profiles_index]) );
                break;
        }
    }

    *p++ = 0x00;  /* non implemented srtp_mki vector length is always 0 */
    /* total extension length: extension type (2 bytes) + extension length (2 bytes) + protection profile length (2 bytes) + 2*nb protection profiles + srtp_mki vector length(1 byte)*/
    *olen = 2 + 2 + 2 + 2*(ssl->dtls_srtp_profiles_list_len) + 1;
}
#endif /* POLARSSL_SSL_PROTO_DTLS */

/*
 * Generate random bytes for ClientHello
 */
static int ssl_generate_random( ssl_context *ssl )
{
    int ret;
    unsigned char *p = ssl->handshake->randbytes;
#if defined(POLARSSL_HAVE_TIME)
    time_t t;
#endif

    /*
     * When responding to a verify request, MUST reuse random (RFC 6347 4.2.1)
     */
#if defined(POLARSSL_SSL_PROTO_DTLS)
    if( ssl->transport == SSL_TRANSPORT_DATAGRAM &&
        ssl->handshake->verify_cookie != NULL )
    {
        return( 0 );
    }
#endif

#if defined(POLARSSL_HAVE_TIME)
    t = time( NULL );
    *p++ = (unsigned char)( t >> 24 );
    *p++ = (unsigned char)( t >> 16 );
    *p++ = (unsigned char)( t >>  8 );
    *p++ = (unsigned char)( t       );

    SSL_DEBUG_MSG( 3, ( "client hello, current time: %lu", t ) );
#else
    if( ( ret = ssl->f_rng( ssl->p_rng, p, 4 ) ) != 0 )
        return( ret );

    p += 4;
#endif /* POLARSSL_HAVE_TIME */

    if( ( ret = ssl->f_rng( ssl->p_rng, p, 28 ) ) != 0 )
        return( ret );

    return( 0 );
}

static int ssl_write_client_hello( ssl_context *ssl )
{
    int ret;
    size_t i, n, olen, ext_len = 0;
    unsigned char *buf;
    unsigned char *p, *q;
    unsigned char offer_compress;
    const int *ciphersuites;
    const ssl_ciphersuite_t *ciphersuite_info;

    SSL_DEBUG_MSG( 2, ( "=> write client hello" ) );

    if( ssl->f_rng == NULL )
    {
        SSL_DEBUG_MSG( 1, ( "no RNG provided") );
        return( POLARSSL_ERR_SSL_NO_RNG );
    }

    if( ssl->renegotiation == SSL_INITIAL_HANDSHAKE )
    {
        ssl->major_ver = ssl->min_major_ver;
        ssl->minor_ver = ssl->min_minor_ver;
    }

    if( ssl->max_major_ver == 0 && ssl->max_minor_ver == 0 )
    {
        ssl->max_major_ver = SSL_MAX_MAJOR_VERSION;
        ssl->max_minor_ver = SSL_MAX_MINOR_VERSION;
    }

    /*
     *     0  .   0   handshake type
     *     1  .   3   handshake length
     *     4  .   5   highest version supported
     *     6  .   9   current UNIX time
     *    10  .  37   random bytes
     */
    buf = ssl->out_msg;
    p = buf + 4;

    ssl_write_version( ssl->max_major_ver, ssl->max_minor_ver,
                       ssl->transport, p );
    p += 2;

    SSL_DEBUG_MSG( 3, ( "client hello, max version: [%d:%d]",
                   buf[4], buf[5] ) );

    if( ( ret = ssl_generate_random( ssl ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "ssl_generate_random", ret );
        return( ret );
    }

    memcpy( p, ssl->handshake->randbytes, 32 );
    SSL_DEBUG_BUF( 3, "client hello, random bytes", p, 32 );
    p += 32;

    /*
     *    38  .  38   session id length
     *    39  . 39+n  session id
     *   39+n . 39+n  DTLS only: cookie length (1 byte)
     *   40+n .  ..   DTSL only: cookie
     *   ..   . ..    ciphersuitelist length (2 bytes)
     *   ..   . ..    ciphersuitelist
     *   ..   . ..    compression methods length (1 byte)
     *   ..   . ..    compression methods
     *   ..   . ..    extensions length (2 bytes)
     *   ..   . ..    extensions
     */
    n = ssl->session_negotiate->length;

    if( ssl->renegotiation != SSL_INITIAL_HANDSHAKE || n < 16 || n > 32 ||
        ssl->handshake->resume == 0 )
    {
        n = 0;
    }

#if defined(POLARSSL_SSL_SESSION_TICKETS)
    /*
     * RFC 5077 section 3.4: "When presenting a ticket, the client MAY
     * generate and include a Session ID in the TLS ClientHello."
     */
    if( ssl->renegotiation == SSL_INITIAL_HANDSHAKE &&
        ssl->session_negotiate->ticket != NULL &&
        ssl->session_negotiate->ticket_len != 0 )
    {
        ret = ssl->f_rng( ssl->p_rng, ssl->session_negotiate->id, 32 );

        if( ret != 0 )
            return( ret );

        ssl->session_negotiate->length = n = 32;
    }
#endif /* POLARSSL_SSL_SESSION_TICKETS */

    *p++ = (unsigned char) n;

    for( i = 0; i < n; i++ )
        *p++ = ssl->session_negotiate->id[i];

    SSL_DEBUG_MSG( 3, ( "client hello, session id len.: %d", n ) );
    SSL_DEBUG_BUF( 3,   "client hello, session id", buf + 39, n );

    /*
     * DTLS cookie
     */
#if defined(POLARSSL_SSL_PROTO_DTLS)
    if( ssl->transport == SSL_TRANSPORT_DATAGRAM )
    {
        if( ssl->handshake->verify_cookie == NULL )
        {
            SSL_DEBUG_MSG( 3, ( "no verify cookie to send" ) );
            *p++ = 0;
        }
        else
        {
            SSL_DEBUG_BUF( 3, "client hello, cookie",
                              ssl->handshake->verify_cookie,
                              ssl->handshake->verify_cookie_len );

            *p++ = ssl->handshake->verify_cookie_len;
            memcpy( p, ssl->handshake->verify_cookie,
                       ssl->handshake->verify_cookie_len );
            p += ssl->handshake->verify_cookie_len;
        }
    }
#endif

    /*
     * Ciphersuite list
     */
    ciphersuites = ssl->ciphersuite_list[ssl->minor_ver];

    /* Skip writing ciphersuite length for now */
    n = 0;
    q = p;
    p += 2;

    /* Add TLS_EMPTY_RENEGOTIATION_INFO_SCSV */
    if( ssl->renegotiation == SSL_INITIAL_HANDSHAKE )
    {
        *p++ = (unsigned char)( SSL_EMPTY_RENEGOTIATION_INFO >> 8 );
        *p++ = (unsigned char)( SSL_EMPTY_RENEGOTIATION_INFO      );
        n++;
    }

    for( i = 0; ciphersuites[i] != 0; i++ )
    {
        ciphersuite_info = ssl_ciphersuite_from_id( ciphersuites[i] );

        if( ciphersuite_info == NULL )
            continue;

        if( ciphersuite_info->min_minor_ver > ssl->max_minor_ver ||
            ciphersuite_info->max_minor_ver < ssl->min_minor_ver )
            continue;

#if defined(POLARSSL_SSL_PROTO_DTLS)
        if( ssl->transport == SSL_TRANSPORT_DATAGRAM &&
            ( ciphersuite_info->flags & POLARSSL_CIPHERSUITE_NODTLS ) )
            continue;
#endif

        SSL_DEBUG_MSG( 3, ( "client hello, add ciphersuite: %2d",
                       ciphersuites[i] ) );

        n++;
        *p++ = (unsigned char)( ciphersuites[i] >> 8 );
        *p++ = (unsigned char)( ciphersuites[i]      );
    }

    *q++ = (unsigned char)( n >> 7 );
    *q++ = (unsigned char)( n << 1 );

    SSL_DEBUG_MSG( 3, ( "client hello, got %d ciphersuites", n ) );

#if defined(POLARSSL_ZLIB_SUPPORT)
    offer_compress = 1;
#else
    offer_compress = 0;
#endif

    /*
     * We don't support compression with DTLS right now: is many records come
     * in the same datagram, uncompressing one could overwrite the next one.
     * We don't want to add complexity for handling that case unless there is
     * an actual need for it.
     */
#if defined(POLARSSL_SSL_PROTO_DTLS)
    if( ssl->transport == SSL_TRANSPORT_DATAGRAM )
        offer_compress = 0;
#endif

    if( offer_compress )
    {
        SSL_DEBUG_MSG( 3, ( "client hello, compress len.: %d", 2 ) );
        SSL_DEBUG_MSG( 3, ( "client hello, compress alg.: %d %d",
                            SSL_COMPRESS_DEFLATE, SSL_COMPRESS_NULL ) );

        *p++ = 2;
        *p++ = SSL_COMPRESS_DEFLATE;
        *p++ = SSL_COMPRESS_NULL;
    }
    else
    {
        SSL_DEBUG_MSG( 3, ( "client hello, compress len.: %d", 1 ) );
        SSL_DEBUG_MSG( 3, ( "client hello, compress alg.: %d",
                            SSL_COMPRESS_NULL ) );

        *p++ = 1;
        *p++ = SSL_COMPRESS_NULL;
    }

    // First write extensions, then the total length
    //
#if defined(POLARSSL_SSL_SERVER_NAME_INDICATION)
    ssl_write_hostname_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;
#endif

    ssl_write_renegotiation_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;

#if defined(POLARSSL_SSL_PROTO_TLS1_2)
    ssl_write_signature_algorithms_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;
#endif

#if defined(POLARSSL_ECDH_C) || defined(POLARSSL_ECDSA_C)
    ssl_write_supported_elliptic_curves_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;

    ssl_write_supported_point_formats_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;
#endif

#if defined(POLARSSL_SSL_MAX_FRAGMENT_LENGTH)
    ssl_write_max_fragment_length_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;
#endif

#if defined(POLARSSL_SSL_TRUNCATED_HMAC)
    ssl_write_truncated_hmac_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;
#endif

#if defined(POLARSSL_SSL_SESSION_TICKETS)
    ssl_write_session_ticket_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;
#endif

#if defined(POLARSSL_SSL_ALPN)
    ssl_write_alpn_ext( ssl, p + 2 + ext_len, &olen );
    ext_len += olen;
#endif

#if defined(POLARSSL_SSL_PROTO_DTLS)
	ssl_write_use_srtp_ext( ssl, p + 2 + ext_len, &olen );
	ext_len += olen;
#endif

    SSL_DEBUG_MSG( 3, ( "client hello, total extension length: %d",
                   ext_len ) );

    if( ext_len > 0 )
    {
        *p++ = (unsigned char)( ( ext_len >> 8 ) & 0xFF );
        *p++ = (unsigned char)( ( ext_len      ) & 0xFF );
        p += ext_len;
    }

    ssl->out_msglen  = p - buf;
    ssl->out_msgtype = SSL_MSG_HANDSHAKE;
    ssl->out_msg[0]  = SSL_HS_CLIENT_HELLO;

    ssl->state++;

#if defined(POLARSSL_SSL_PROTO_DTLS)
    if( ssl->transport == SSL_TRANSPORT_DATAGRAM )
        ssl_send_flight_completed( ssl );
#endif

    if( ( ret = ssl_write_record( ssl ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "ssl_write_record", ret );
        return( ret );
    }

    SSL_DEBUG_MSG( 2, ( "<= write client hello" ) );

    return( 0 );
}

static int ssl_parse_renegotiation_info( ssl_context *ssl,
                                         const unsigned char *buf,
                                         size_t len )
{
    int ret;

    if( ssl->renegotiation == SSL_INITIAL_HANDSHAKE )
    {
        if( len != 1 || buf[0] != 0x0 )
        {
            SSL_DEBUG_MSG( 1, ( "non-zero length renegotiated connection field" ) );

            if( ( ret = ssl_send_fatal_handshake_failure( ssl ) ) != 0 )
                return( ret );

            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
        }

        ssl->secure_renegotiation = SSL_SECURE_RENEGOTIATION;
    }
    else
    {
        /* Check verify-data in constant-time. The length OTOH is no secret */
        if( len    != 1 + ssl->verify_data_len * 2 ||
            buf[0] !=     ssl->verify_data_len * 2 ||
            safer_memcmp( buf + 1,
                          ssl->own_verify_data, ssl->verify_data_len ) != 0 ||
            safer_memcmp( buf + 1 + ssl->verify_data_len,
                          ssl->peer_verify_data, ssl->verify_data_len ) != 0 )
        {
            SSL_DEBUG_MSG( 1, ( "non-matching renegotiated connection field" ) );

            if( ( ret = ssl_send_fatal_handshake_failure( ssl ) ) != 0 )
                return( ret );

            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
        }
    }

    return( 0 );
}

#if defined(POLARSSL_SSL_MAX_FRAGMENT_LENGTH)
static int ssl_parse_max_fragment_length_ext( ssl_context *ssl,
                                              const unsigned char *buf,
                                              size_t len )
{
    /*
     * server should use the extension only if we did,
     * and if so the server's value should match ours (and len is always 1)
     */
    if( ssl->mfl_code == SSL_MAX_FRAG_LEN_NONE ||
        len != 1 ||
        buf[0] != ssl->mfl_code )
    {
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }

    return( 0 );
}
#endif /* POLARSSL_SSL_MAX_FRAGMENT_LENGTH */

#if defined(POLARSSL_SSL_TRUNCATED_HMAC)
static int ssl_parse_truncated_hmac_ext( ssl_context *ssl,
                                         const unsigned char *buf,
                                         size_t len )
{
    if( ssl->trunc_hmac == SSL_TRUNC_HMAC_DISABLED ||
        len != 0 )
    {
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }

    ((void) buf);

    ssl->session_negotiate->trunc_hmac = SSL_TRUNC_HMAC_ENABLED;

    return( 0 );
}
#endif /* POLARSSL_SSL_TRUNCATED_HMAC */

#if defined(POLARSSL_SSL_SESSION_TICKETS)
static int ssl_parse_session_ticket_ext( ssl_context *ssl,
                                         const unsigned char *buf,
                                         size_t len )
{
    if( ssl->session_tickets == SSL_SESSION_TICKETS_DISABLED ||
        len != 0 )
    {
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }

    ((void) buf);

    ssl->handshake->new_session_ticket = 1;

    return( 0 );
}
#endif /* POLARSSL_SSL_SESSION_TICKETS */

#if defined(POLARSSL_ECDH_C) || defined(POLARSSL_ECDSA_C)
static int ssl_parse_supported_point_formats_ext( ssl_context *ssl,
                                                  const unsigned char *buf,
                                                  size_t len )
{
    size_t list_size;
    const unsigned char *p;

    list_size = buf[0];
    if( list_size + 1 != len )
    {
        SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }

    p = buf + 1;
    while( list_size > 0 )
    {
        if( p[0] == POLARSSL_ECP_PF_UNCOMPRESSED ||
            p[0] == POLARSSL_ECP_PF_COMPRESSED )
        {
            ssl->handshake->ecdh_ctx.point_format = p[0];
            SSL_DEBUG_MSG( 4, ( "point format selected: %d", p[0] ) );
            return( 0 );
        }

        list_size--;
        p++;
    }

    SSL_DEBUG_MSG( 1, ( "no point format in common" ) );
    return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
}
#endif /* POLARSSL_ECDH_C || POLARSSL_ECDSA_C */

#if defined(POLARSSL_SSL_ALPN)
static int ssl_parse_alpn_ext( ssl_context *ssl,
                               const unsigned char *buf, size_t len )
{
    size_t list_len, name_len;
    const char **p;

    /* If we didn't send it, the server shouldn't send it */
    if( ssl->alpn_list == NULL )
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );

    /*
     * opaque ProtocolName<1..2^8-1>;
     *
     * struct {
     *     ProtocolName protocol_name_list<2..2^16-1>
     * } ProtocolNameList;
     *
     * the "ProtocolNameList" MUST contain exactly one "ProtocolName"
     */

    /* Min length is 2 (list_len) + 1 (name_len) + 1 (name) */
    if( len < 4 )
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );

    list_len = ( buf[0] << 8 ) | buf[1];
    if( list_len != len - 2 )
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );

    name_len = buf[2];
    if( name_len != list_len - 1 )
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );

    /* Check that the server chosen protocol was in our list and save it */
    for( p = ssl->alpn_list; *p != NULL; p++ )
    {
        if( name_len == strlen( *p ) &&
            memcmp( buf + 3, *p, name_len ) == 0 )
        {
            ssl->alpn_chosen = *p;
            return( 0 );
        }
    }

    return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
}
#endif /* POLARSSL_SSL_ALPN */

#if defined(POLARSSL_SSL_PROTO_DTLS)
static int ssl_parse_use_srtp_ext( ssl_context *ssl,
                               const unsigned char *buf, size_t len )
{
    enum DTLS_SRTP_protection_profiles server_protection = SRTP_UNSET_PROFILE;
    size_t i;
    uint16_t server_protection_profile_value = 0;

    /* If use_srtp is not configured, just ignore the extension */
    if( ( ssl->dtls_srtp_profiles_list == NULL ) || ( ssl->dtls_srtp_profiles_list_len == 0 ) )
        return( 0 );

    /* RFC5764 section 4.1.1
     * uint8 SRTPProtectionProfile[2];
     * 
     * struct {
     *   SRTPProtectionProfiles SRTPProtectionProfiles;
     *   opaque srtp_mki<0..255>;
     * } UseSRTPData;

     * SRTPProtectionProfile SRTPProtectionProfiles<2..2^16-1>;
     *
     * Note: srtp_mki is not supported
     */

    /* Length is 5 : one protection profile(2 bytes) + length(2 bytes) and potential srtp_mki which won't be parsed */
    if( len < 5 )
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );

    /*
     * get the server protection profile
     */
    if (((uint16_t)(buf[0]<<8 | buf[1])) != 0x0002) { /* protection profile length must be 0x0002 as we must have only one protection profile in server Hello */
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    } else {
        server_protection_profile_value = buf[2]<<8 | buf[3];
    }
    

    /*
     * Check we have the server profile in our list
     */
    for( i=0; i < ssl->dtls_srtp_profiles_list_len; i++)
    {
        switch ( server_protection_profile_value ) {
            case SRTP_AES128_CM_HMAC_SHA1_80_IANA_VALUE:
                server_protection = SRTP_AES128_CM_HMAC_SHA1_80;
                break;
            case SRTP_AES128_CM_HMAC_SHA1_32_IANA_VALUE:
                server_protection = SRTP_AES128_CM_HMAC_SHA1_32;
                break;
            case SRTP_NULL_HMAC_SHA1_80_IANA_VALUE:
                server_protection = SRTP_NULL_HMAC_SHA1_80;
                break;
            case SRTP_NULL_HMAC_SHA1_32_IANA_VALUE:
                server_protection = SRTP_NULL_HMAC_SHA1_32;
                break;
            default:
                server_protection = SRTP_UNSET_PROFILE;
                break;
        }

        if (server_protection == ssl->dtls_srtp_profiles_list[i]) {
            ssl->chosen_dtls_srtp_profile = ssl->dtls_srtp_profiles_list[i];
            return 0;
        }
    }

    /* If we get there, no match was found : server problem, it shall never answer with incompatible profile */
    ssl->chosen_dtls_srtp_profile = SRTP_UNSET_PROFILE;
    ssl_send_alert_message( ssl, SSL_ALERT_LEVEL_FATAL,
                            SSL_ALERT_MSG_HANDSHAKE_FAILURE );
    return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
}
#endif /* POLARSSL_SSL_PROTO_DTLS */

/*
 * Parse HelloVerifyRequest.  Only called after verifying the HS type.
 */
#if defined(POLARSSL_SSL_PROTO_DTLS)
static int ssl_parse_hello_verify_request( ssl_context *ssl )
{
    const unsigned char *p = ssl->in_msg + ssl_hs_hdr_len( ssl );
    int major_ver, minor_ver;
    unsigned char cookie_len;

    SSL_DEBUG_MSG( 2, ( "=> parse hello verify request" ) );

    /*
     * struct {
     *   ProtocolVersion server_version;
     *   opaque cookie<0..2^8-1>;
     * } HelloVerifyRequest;
     */
    SSL_DEBUG_BUF( 3, "server version", (unsigned char *) p, 2 );
    ssl_read_version( &major_ver, &minor_ver, ssl->transport, p );
    p += 2;

    /*
     * Since the RFC is not clear on this point, accept DTLS 1.0 (TLS 1.1)
     * even is lower than our min version.
     */
    if( major_ver < SSL_MAJOR_VERSION_3 ||
        minor_ver < SSL_MINOR_VERSION_2 ||
        major_ver > ssl->max_major_ver  ||
        minor_ver > ssl->max_minor_ver  )
    {
        SSL_DEBUG_MSG( 1, ( "bad server version" ) );

        ssl_send_alert_message( ssl, SSL_ALERT_LEVEL_FATAL,
                                     SSL_ALERT_MSG_PROTOCOL_VERSION );

        return( POLARSSL_ERR_SSL_BAD_HS_PROTOCOL_VERSION );
    }

    cookie_len = *p++;
    SSL_DEBUG_BUF( 3, "cookie", (unsigned char *) p, cookie_len );

    polarssl_free( ssl->handshake->verify_cookie );

    ssl->handshake->verify_cookie = polarssl_malloc( cookie_len );
    if( ssl->handshake->verify_cookie  == NULL )
    {
        SSL_DEBUG_MSG( 1, ( "malloc failed (%d bytes)", cookie_len ) );
        return( POLARSSL_ERR_SSL_MALLOC_FAILED );
    }

    memcpy( ssl->handshake->verify_cookie, p, cookie_len );
    ssl->handshake->verify_cookie_len = cookie_len;

    /* Start over at ClientHello */
    ssl->state = SSL_CLIENT_HELLO;
    ssl_reset_checksum( ssl );

    ssl_recv_flight_completed( ssl );

    SSL_DEBUG_MSG( 2, ( "<= parse hello verify request" ) );

    return( 0 );
}
#endif /* POLARSSL_SSL_PROTO_DTLS */

static int ssl_parse_server_hello( ssl_context *ssl )
{
    int ret, i;
    size_t n;
    size_t ext_len;
    unsigned char *buf, *ext;
    unsigned char comp, accept_comp;
    int renegotiation_info_seen = 0;
    int handshake_failure = 0;
#if defined(POLARSSL_DEBUG_C)
    uint32_t t;
#endif

    SSL_DEBUG_MSG( 2, ( "=> parse server hello" ) );

    buf = ssl->in_msg;

    if( ( ret = ssl_read_record( ssl ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "ssl_read_record", ret );
        return( ret );
    }

    if( ssl->in_msgtype != SSL_MSG_HANDSHAKE )
    {
        if( ssl->renegotiation == SSL_RENEGOTIATION )
        {
            ssl->renego_records_seen++;

            if( ssl->renego_max_records >= 0 &&
                ssl->renego_records_seen > ssl->renego_max_records )
            {
                SSL_DEBUG_MSG( 1, ( "renegotiation requested, "
                                    "but not honored by server" ) );
                return( POLARSSL_ERR_SSL_UNEXPECTED_MESSAGE );
            }

            SSL_DEBUG_MSG( 1, ( "non-handshake message during renego" ) );
            return( POLARSSL_ERR_SSL_WAITING_SERVER_HELLO_RENEGO );
        }

        SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
        return( POLARSSL_ERR_SSL_UNEXPECTED_MESSAGE );
    }

#if defined(POLARSSL_SSL_PROTO_DTLS)
    if( ssl->transport == SSL_TRANSPORT_DATAGRAM )
    {
        if( buf[0] == SSL_HS_HELLO_VERIFY_REQUEST )
        {
            SSL_DEBUG_MSG( 2, ( "received hello verify request" ) );
            SSL_DEBUG_MSG( 2, ( "<= parse server hello" ) );
            return( ssl_parse_hello_verify_request( ssl ) );
        }
        else
        {
            /* We made it through the verification process */
            polarssl_free( ssl->handshake->verify_cookie );
            ssl->handshake->verify_cookie = NULL;
            ssl->handshake->verify_cookie_len = 0;
        }
    }
#endif /* POLARSSL_SSL_PROTO_DTLS */

    if( ssl->in_hslen < 38 + ssl_hs_hdr_len( ssl ) ||
        buf[0] != SSL_HS_SERVER_HELLO )
    {
        SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }

    /*
     *  0   .  1    server_version
     *  2   . 33    random (maybe including 4 bytes of Unix time)
     * 34   . 34    session_id length = n
     * 35   . 34+n  session_id
     * 35+n . 36+n  cipher_suite
     * 37+n . 37+n  compression_method
     *
     * 38+n . 39+n  extensions length (optional)
     * 40+n .  ..   extensions
     */
    buf += ssl_hs_hdr_len( ssl );

    SSL_DEBUG_BUF( 3, "server hello, version", buf + 0, 2 );
    ssl_read_version( &ssl->major_ver, &ssl->minor_ver,
                      ssl->transport, buf + 0 );

    if( ssl->major_ver < ssl->min_major_ver ||
        ssl->minor_ver < ssl->min_minor_ver ||
        ssl->major_ver > ssl->max_major_ver ||
        ssl->minor_ver > ssl->max_minor_ver )
    {
        SSL_DEBUG_MSG( 1, ( "server version out of bounds - "
                            " min: [%d:%d], server: [%d:%d], max: [%d:%d]",
                            ssl->min_major_ver, ssl->min_minor_ver,
                            ssl->major_ver, ssl->minor_ver,
                            ssl->max_major_ver, ssl->max_minor_ver ) );

        ssl_send_alert_message( ssl, SSL_ALERT_LEVEL_FATAL,
                                     SSL_ALERT_MSG_PROTOCOL_VERSION );

        return( POLARSSL_ERR_SSL_BAD_HS_PROTOCOL_VERSION );
    }

#if defined(POLARSSL_DEBUG_C)
    t = ( (uint32_t) buf[2] << 24 )
      | ( (uint32_t) buf[3] << 16 )
      | ( (uint32_t) buf[4] <<  8 )
      | ( (uint32_t) buf[5]       );
    SSL_DEBUG_MSG( 3, ( "server hello, current time: %lu", t ) );
#endif

    memcpy( ssl->handshake->randbytes + 32, buf + 2, 32 );

    n = buf[34];

    SSL_DEBUG_BUF( 3,   "server hello, random bytes", buf + 2, 32 );

    if( n > 32 )
    {
        SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }

    if( ssl->in_hslen > 39 + n )
    {
        ext_len = ( ( buf[38 + n] <<  8 )
                  | ( buf[39 + n]       ) );

        if( ( ext_len > 0 && ext_len < 4 ) ||
            ssl->in_hslen != ssl_hs_hdr_len( ssl ) + 40 + n + ext_len )
        {
            SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
        }
    }
    else if( ssl->in_hslen == 38 + n )
    {
        ext_len = 0;
    }
    else
    {
        SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }

    /* ciphersuite (used later) */
    i = ( buf[35 + n] << 8 ) | buf[36 + n];

    /*
     * Read and check compression
     */
    comp = buf[37 + n];

#if defined(POLARSSL_ZLIB_SUPPORT)
    accept_comp = 1;
#else
    accept_comp = 0;
#endif

    /* See comments in ssl_write_client_hello() */
#if defined(POLARSSL_SSL_PROTO_DTLS)
    if( ssl->transport == SSL_TRANSPORT_DATAGRAM )
        accept_comp = 0;
#endif

    if( ( accept_comp == 0 && comp != SSL_COMPRESS_NULL ) ||
        ( comp != SSL_COMPRESS_NULL && comp != SSL_COMPRESS_DEFLATE ) )
    {
        SSL_DEBUG_MSG( 1, ( "server hello, bad compression: %d", comp ) );
        return( POLARSSL_ERR_SSL_FEATURE_UNAVAILABLE );
    }

    /*
     * Initialize update checksum functions
     */
    ssl->transform_negotiate->ciphersuite_info = ssl_ciphersuite_from_id( i );

    if( ssl->transform_negotiate->ciphersuite_info == NULL )
    {
        SSL_DEBUG_MSG( 1, ( "ciphersuite info for %04x not found", i ) );
        return( POLARSSL_ERR_SSL_BAD_INPUT_DATA );
    }

    ssl_optimize_checksum( ssl, ssl->transform_negotiate->ciphersuite_info );

    SSL_DEBUG_MSG( 3, ( "server hello, session id len.: %d", n ) );
    SSL_DEBUG_BUF( 3,   "server hello, session id", buf + 35, n );

    /*
     * Check if the session can be resumed
     */
    if( ssl->renegotiation != SSL_INITIAL_HANDSHAKE ||
        ssl->handshake->resume == 0 || n == 0 ||
        ssl->session_negotiate->ciphersuite != i ||
        ssl->session_negotiate->compression != comp ||
        ssl->session_negotiate->length != n ||
        memcmp( ssl->session_negotiate->id, buf + 35, n ) != 0 )
    {
        ssl->state++;
        ssl->handshake->resume = 0;
#if defined(POLARSSL_HAVE_TIME)
        ssl->session_negotiate->start = time( NULL );
#endif
        ssl->session_negotiate->ciphersuite = i;
        ssl->session_negotiate->compression = comp;
        ssl->session_negotiate->length = n;
        memcpy( ssl->session_negotiate->id, buf + 35, n );
    }
    else
    {
        ssl->state = SSL_SERVER_CHANGE_CIPHER_SPEC;

        if( ( ret = ssl_derive_keys( ssl ) ) != 0 )
        {
            SSL_DEBUG_RET( 1, "ssl_derive_keys", ret );
            return( ret );
        }
    }

    SSL_DEBUG_MSG( 3, ( "%s session has been resumed",
                   ssl->handshake->resume ? "a" : "no" ) );

    SSL_DEBUG_MSG( 3, ( "server hello, chosen ciphersuite: %d", i ) );
    SSL_DEBUG_MSG( 3, ( "server hello, compress alg.: %d", buf[37 + n] ) );

    i = 0;
    while( 1 )
    {
        if( ssl->ciphersuite_list[ssl->minor_ver][i] == 0 )
        {
            SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
        }

        if( ssl->ciphersuite_list[ssl->minor_ver][i++] ==
            ssl->session_negotiate->ciphersuite )
        {
            break;
        }
    }

    if( comp != SSL_COMPRESS_NULL
#if defined(POLARSSL_ZLIB_SUPPORT)
        && comp != SSL_COMPRESS_DEFLATE
#endif
      )
    {
        SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }
    ssl->session_negotiate->compression = comp;

    ext = buf + 40 + n;

    SSL_DEBUG_MSG( 2, ( "server hello, total extension length: %d", ext_len ) );

    while( ext_len )
    {
        unsigned int ext_id   = ( ( ext[0] <<  8 )
                                | ( ext[1]       ) );
        unsigned int ext_size = ( ( ext[2] <<  8 )
                                | ( ext[3]       ) );

        if( ext_size + 4 > ext_len )
        {
            SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
        }

        switch( ext_id )
        {
        case TLS_EXT_RENEGOTIATION_INFO:
            SSL_DEBUG_MSG( 3, ( "found renegotiation extension" ) );
            renegotiation_info_seen = 1;

            if( ( ret = ssl_parse_renegotiation_info( ssl, ext + 4,
                                                      ext_size ) ) != 0 )
                return( ret );

            break;

#if defined(POLARSSL_SSL_MAX_FRAGMENT_LENGTH)
        case TLS_EXT_MAX_FRAGMENT_LENGTH:
            SSL_DEBUG_MSG( 3, ( "found max_fragment_length extension" ) );

            if( ( ret = ssl_parse_max_fragment_length_ext( ssl,
                            ext + 4, ext_size ) ) != 0 )
            {
                return( ret );
            }

            break;
#endif /* POLARSSL_SSL_MAX_FRAGMENT_LENGTH */

#if defined(POLARSSL_SSL_TRUNCATED_HMAC)
        case TLS_EXT_TRUNCATED_HMAC:
            SSL_DEBUG_MSG( 3, ( "found truncated_hmac extension" ) );

            if( ( ret = ssl_parse_truncated_hmac_ext( ssl,
                            ext + 4, ext_size ) ) != 0 )
            {
                return( ret );
            }

            break;
#endif /* POLARSSL_SSL_TRUNCATED_HMAC */

#if defined(POLARSSL_SSL_SESSION_TICKETS)
        case TLS_EXT_SESSION_TICKET:
            SSL_DEBUG_MSG( 3, ( "found session_ticket extension" ) );

            if( ( ret = ssl_parse_session_ticket_ext( ssl,
                            ext + 4, ext_size ) ) != 0 )
            {
                return( ret );
            }

            break;
#endif /* POLARSSL_SSL_SESSION_TICKETS */

#if defined(POLARSSL_ECDH_C) || defined(POLARSSL_ECDSA_C)
        case TLS_EXT_SUPPORTED_POINT_FORMATS:
            SSL_DEBUG_MSG( 3, ( "found supported_point_formats extension" ) );

            if( ( ret = ssl_parse_supported_point_formats_ext( ssl,
                            ext + 4, ext_size ) ) != 0 )
            {
                return( ret );
            }

            break;
#endif /* POLARSSL_ECDH_C || POLARSSL_ECDSA_C */

#if defined(POLARSSL_SSL_ALPN)
        case TLS_EXT_ALPN:
            SSL_DEBUG_MSG( 3, ( "found alpn extension" ) );

            if( ( ret = ssl_parse_alpn_ext( ssl, ext + 4, ext_size ) ) != 0 )
                return( ret );

            break;
#endif /* POLARSSL_SSL_ALPN */

#if defined(POLARSSL_SSL_PROTO_DTLS)
        case TLS_EXT_USE_SRTP:
            SSL_DEBUG_MSG( 3, ( "found use_srtp extension" ) );

            if( ( ret = ssl_parse_use_srtp_ext( ssl, ext + 4, ext_size ) ) != 0 )
                return( ret );

            break;
#endif /* POLARSSL_SSL_PROTO_DTLS */

        default:
            SSL_DEBUG_MSG( 3, ( "unknown extension found: %d (ignoring)",
                           ext_id ) );
        }

        ext_len -= 4 + ext_size;
        ext += 4 + ext_size;

        if( ext_len > 0 && ext_len < 4 )
        {
            SSL_DEBUG_MSG( 1, ( "bad server hello message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
        }
    }

    /*
     * Renegotiation security checks
     */
    if( ssl->secure_renegotiation == SSL_LEGACY_RENEGOTIATION &&
        ssl->allow_legacy_renegotiation == SSL_LEGACY_BREAK_HANDSHAKE )
    {
        SSL_DEBUG_MSG( 1, ( "legacy renegotiation, breaking off handshake" ) );
        handshake_failure = 1;
    }
    else if( ssl->renegotiation == SSL_RENEGOTIATION &&
             ssl->secure_renegotiation == SSL_SECURE_RENEGOTIATION &&
             renegotiation_info_seen == 0 )
    {
        SSL_DEBUG_MSG( 1, ( "renegotiation_info extension missing (secure)" ) );
        handshake_failure = 1;
    }
    else if( ssl->renegotiation == SSL_RENEGOTIATION &&
             ssl->secure_renegotiation == SSL_LEGACY_RENEGOTIATION &&
             ssl->allow_legacy_renegotiation == SSL_LEGACY_NO_RENEGOTIATION )
    {
        SSL_DEBUG_MSG( 1, ( "legacy renegotiation not allowed" ) );
        handshake_failure = 1;
    }
    else if( ssl->renegotiation == SSL_RENEGOTIATION &&
             ssl->secure_renegotiation == SSL_LEGACY_RENEGOTIATION &&
             renegotiation_info_seen == 1 )
    {
        SSL_DEBUG_MSG( 1, ( "renegotiation_info extension present (legacy)" ) );
        handshake_failure = 1;
    }

    if( handshake_failure == 1 )
    {
        if( ( ret = ssl_send_fatal_handshake_failure( ssl ) ) != 0 )
            return( ret );

        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO );
    }

    SSL_DEBUG_MSG( 2, ( "<= parse server hello" ) );

    return( 0 );
}

#if defined(POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED) ||                       \
    defined(POLARSSL_KEY_EXCHANGE_DHE_PSK_ENABLED)
static int ssl_parse_server_dh_params( ssl_context *ssl, unsigned char **p,
                                       unsigned char *end )
{
    int ret = POLARSSL_ERR_SSL_FEATURE_UNAVAILABLE;

    /*
     * Ephemeral DH parameters:
     *
     * struct {
     *     opaque dh_p<1..2^16-1>;
     *     opaque dh_g<1..2^16-1>;
     *     opaque dh_Ys<1..2^16-1>;
     * } ServerDHParams;
     */
    if( ( ret = dhm_read_params( &ssl->handshake->dhm_ctx, p, end ) ) != 0 )
    {
        SSL_DEBUG_RET( 2, ( "dhm_read_params" ), ret );
        return( ret );
    }

    if( ssl->handshake->dhm_ctx.len < 64  ||
        ssl->handshake->dhm_ctx.len > 512 )
    {
        SSL_DEBUG_MSG( 1, ( "bad server key exchange message (DHM length)" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
    }

    SSL_DEBUG_MPI( 3, "DHM: P ", &ssl->handshake->dhm_ctx.P  );
    SSL_DEBUG_MPI( 3, "DHM: G ", &ssl->handshake->dhm_ctx.G  );
    SSL_DEBUG_MPI( 3, "DHM: GY", &ssl->handshake->dhm_ctx.GY );

    return( ret );
}
#endif /* POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_DHE_PSK_ENABLED */

#if defined(POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED) ||                     \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED) ||                   \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_PSK_ENABLED) ||                     \
    defined(POLARSSL_KEY_EXCHANGE_ECDH_RSA_ENABLED) ||                      \
    defined(POLARSSL_KEY_EXCHANGE_ECDH_ECDSA_ENABLED)
static int ssl_check_server_ecdh_params( const ssl_context *ssl )
{
    const ecp_curve_info *curve_info;

    curve_info = ecp_curve_info_from_grp_id( ssl->handshake->ecdh_ctx.grp.id );
    if( curve_info == NULL )
    {
        SSL_DEBUG_MSG( 1, ( "should never happen" ) );
        return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
    }

    SSL_DEBUG_MSG( 2, ( "ECDH curve: %s", curve_info->name ) );

#if defined(POLARSSL_SSL_ECP_SET_CURVES)
    if( ! ssl_curve_is_acceptable( ssl, ssl->handshake->ecdh_ctx.grp.id ) )
#else
    if( ssl->handshake->ecdh_ctx.grp.nbits < 163 ||
        ssl->handshake->ecdh_ctx.grp.nbits > 521 )
#endif
        return( -1 );

    SSL_DEBUG_ECP( 3, "ECDH: Qp", &ssl->handshake->ecdh_ctx.Qp );

    return( 0 );
}
#endif /* POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_PSK_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDH_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDH_ECDSA_ENABLED */

#if defined(POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED) ||                     \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED) ||                   \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_PSK_ENABLED)
static int ssl_parse_server_ecdh_params( ssl_context *ssl,
                                         unsigned char **p,
                                         unsigned char *end )
{
    int ret = POLARSSL_ERR_SSL_FEATURE_UNAVAILABLE;

    /*
     * Ephemeral ECDH parameters:
     *
     * struct {
     *     ECParameters curve_params;
     *     ECPoint      public;
     * } ServerECDHParams;
     */
    if( ( ret = ecdh_read_params( &ssl->handshake->ecdh_ctx,
                                  (const unsigned char **) p, end ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, ( "ecdh_read_params" ), ret );
        return( ret );
    }

    if( ssl_check_server_ecdh_params( ssl ) != 0 )
    {
        SSL_DEBUG_MSG( 1, ( "bad server key exchange message (ECDHE curve)" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
    }

    return( ret );
}
#endif /* POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_PSK_ENABLED */

#if defined(POLARSSL_KEY_EXCHANGE__SOME__PSK_ENABLED)
static int ssl_parse_server_psk_hint( ssl_context *ssl,
                                      unsigned char **p,
                                      unsigned char *end )
{
    int ret = POLARSSL_ERR_SSL_FEATURE_UNAVAILABLE;
    size_t  len;
    ((void) ssl);

    /*
     * PSK parameters:
     *
     * opaque psk_identity_hint<0..2^16-1>;
     */
    len = (*p)[0] << 8 | (*p)[1];
    *p += 2;

    if( (*p) + len > end )
    {
        SSL_DEBUG_MSG( 1, ( "bad server key exchange message (psk_identity_hint length)" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
    }

    // TODO: Retrieve PSK identity hint and callback to app
    //
    *p += len;
    ret = 0;

    return( ret );
}
#endif /* POLARSSL_KEY_EXCHANGE__SOME__PSK_ENABLED */

#if defined(POLARSSL_KEY_EXCHANGE_RSA_ENABLED) ||                           \
    defined(POLARSSL_KEY_EXCHANGE_RSA_PSK_ENABLED)
/*
 * Generate a pre-master secret and encrypt it with the server's RSA key
 */
static int ssl_write_encrypted_pms( ssl_context *ssl,
                                    size_t offset, size_t *olen,
                                    size_t pms_offset )
{
    int ret;
    size_t len_bytes = ssl->minor_ver == SSL_MINOR_VERSION_0 ? 0 : 2;
    unsigned char *p = ssl->handshake->premaster + pms_offset;

    /*
     * Generate (part of) the pre-master as
     *  struct {
     *      ProtocolVersion client_version;
     *      opaque random[46];
     *  } PreMasterSecret;
     */
    ssl_write_version( ssl->max_major_ver, ssl->max_minor_ver,
                       ssl->transport, p );

    if( ( ret = ssl->f_rng( ssl->p_rng, p + 2, 46 ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "f_rng", ret );
        return( ret );
    }

    ssl->handshake->pmslen = 48;

    /*
     * Now write it out, encrypted
     */
    if( ! pk_can_do( &ssl->session_negotiate->peer_cert->pk,
                POLARSSL_PK_RSA ) )
    {
        SSL_DEBUG_MSG( 1, ( "certificate key type mismatch" ) );
        return( POLARSSL_ERR_SSL_PK_TYPE_MISMATCH );
    }

    if( ( ret = pk_encrypt( &ssl->session_negotiate->peer_cert->pk,
                            p, ssl->handshake->pmslen,
                            ssl->out_msg + offset + len_bytes, olen,
                            SSL_MAX_CONTENT_LEN - offset - len_bytes,
                            ssl->f_rng, ssl->p_rng ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "rsa_pkcs1_encrypt", ret );
        return( ret );
    }

#if defined(POLARSSL_SSL_PROTO_TLS1) || defined(POLARSSL_SSL_PROTO_TLS1_1) || \
    defined(POLARSSL_SSL_PROTO_TLS1_2)
    if( len_bytes == 2 )
    {
        ssl->out_msg[offset+0] = (unsigned char)( *olen >> 8 );
        ssl->out_msg[offset+1] = (unsigned char)( *olen      );
        *olen += 2;
    }
#endif

    return( 0 );
}
#endif /* POLARSSL_KEY_EXCHANGE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_RSA_PSK_ENABLED */

#if defined(POLARSSL_SSL_PROTO_TLS1_2)
#if defined(POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED) ||                       \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED) ||                     \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED)
static int ssl_parse_signature_algorithm( ssl_context *ssl,
                                          unsigned char **p,
                                          unsigned char *end,
                                          md_type_t *md_alg,
                                          pk_type_t *pk_alg )
{
    ((void) ssl);
    *md_alg = POLARSSL_MD_NONE;
    *pk_alg = POLARSSL_PK_NONE;

    /* Only in TLS 1.2 */
    if( ssl->minor_ver != SSL_MINOR_VERSION_3 )
    {
        return( 0 );
    }

    if( (*p) + 2 > end )
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );

    /*
     * Get hash algorithm
     */
    if( ( *md_alg = ssl_md_alg_from_hash( (*p)[0] ) ) == POLARSSL_MD_NONE )
    {
        SSL_DEBUG_MSG( 2, ( "Server used unsupported "
                            "HashAlgorithm %d", *(p)[0] ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
    }

    /*
     * Get signature algorithm
     */
    if( ( *pk_alg = ssl_pk_alg_from_sig( (*p)[1] ) ) == POLARSSL_PK_NONE )
    {
        SSL_DEBUG_MSG( 2, ( "server used unsupported "
                            "SignatureAlgorithm %d", (*p)[1] ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
    }

    SSL_DEBUG_MSG( 2, ( "Server used SignatureAlgorithm %d", (*p)[1] ) );
    SSL_DEBUG_MSG( 2, ( "Server used HashAlgorithm %d", (*p)[0] ) );
    *p += 2;

    return( 0 );
}
#endif /* POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED */
#endif /* POLARSSL_SSL_PROTO_TLS1_2 */


#if defined(POLARSSL_KEY_EXCHANGE_ECDH_RSA_ENABLED) || \
    defined(POLARSSL_KEY_EXCHANGE_ECDH_ECDSA_ENABLED)
static int ssl_get_ecdh_params_from_cert( ssl_context *ssl )
{
    int ret;
    const ecp_keypair *peer_key;

    if( ! pk_can_do( &ssl->session_negotiate->peer_cert->pk,
                     POLARSSL_PK_ECKEY ) )
    {
        SSL_DEBUG_MSG( 1, ( "server key not ECDH capable" ) );
        return( POLARSSL_ERR_SSL_PK_TYPE_MISMATCH );
    }

    peer_key = pk_ec( ssl->session_negotiate->peer_cert->pk );

    if( ( ret = ecdh_get_params( &ssl->handshake->ecdh_ctx, peer_key,
                                 POLARSSL_ECDH_THEIRS ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, ( "ecdh_get_params" ), ret );
        return( ret );
    }

    if( ssl_check_server_ecdh_params( ssl ) != 0 )
    {
        SSL_DEBUG_MSG( 1, ( "bad server certificate (ECDH curve)" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_CERTIFICATE );
    }

    return( ret );
}
#endif /* POLARSSL_KEY_EXCHANGE_ECDH_RSA_ENABLED) ||
          POLARSSL_KEY_EXCHANGE_ECDH_ECDSA_ENABLED */

static int ssl_parse_server_key_exchange( ssl_context *ssl )
{
    int ret;
    const ssl_ciphersuite_t *ciphersuite_info = ssl->transform_negotiate->ciphersuite_info;
    unsigned char *p, *end;

    SSL_DEBUG_MSG( 2, ( "=> parse server key exchange" ) );

#if defined(POLARSSL_KEY_EXCHANGE_RSA_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA )
    {
        SSL_DEBUG_MSG( 2, ( "<= skip parse server key exchange" ) );
        ssl->state++;
        return( 0 );
    }
    ((void) p);
    ((void) end);
#endif

#if defined(POLARSSL_KEY_EXCHANGE_ECDH_RSA_ENABLED) || \
    defined(POLARSSL_KEY_EXCHANGE_ECDH_ECDSA_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDH_RSA ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDH_ECDSA )
    {
        if( ( ret = ssl_get_ecdh_params_from_cert( ssl ) ) != 0 )
        {
            SSL_DEBUG_RET( 1, "ssl_get_ecdh_params_from_cert", ret );
            return( ret );
        }

        SSL_DEBUG_MSG( 2, ( "<= skip parse server key exchange" ) );
        ssl->state++;
        return( 0 );
    }
    ((void) p);
    ((void) end);
#endif /* POLARSSL_KEY_EXCHANGE_ECDH_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDH_ECDSA_ENABLED */

    if( ( ret = ssl_read_record( ssl ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "ssl_read_record", ret );
        return( ret );
    }

    if( ssl->in_msgtype != SSL_MSG_HANDSHAKE )
    {
        SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
        return( POLARSSL_ERR_SSL_UNEXPECTED_MESSAGE );
    }

    /*
     * ServerKeyExchange may be skipped with PSK and RSA-PSK when the server
     * doesn't use a psk_identity_hint
     */
    if( ssl->in_msg[0] != SSL_HS_SERVER_KEY_EXCHANGE )
    {
        if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK ||
            ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK )
        {
            ssl->record_read = 1;
            goto exit;
        }

        SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
        return( POLARSSL_ERR_SSL_UNEXPECTED_MESSAGE );
    }

    p   = ssl->in_msg + ssl_hs_hdr_len( ssl );
    end = ssl->in_msg + ssl->in_hslen;
    SSL_DEBUG_BUF( 3,   "server key exchange", p, end - p );

#if defined(POLARSSL_KEY_EXCHANGE__SOME__PSK_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_PSK )
    {
        if( ssl_parse_server_psk_hint( ssl, &p, end ) != 0 )
        {
            SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
        }
    } /* FALLTROUGH */
#endif /* POLARSSL_KEY_EXCHANGE__SOME__PSK_ENABLED */

#if defined(POLARSSL_KEY_EXCHANGE_PSK_ENABLED) ||                       \
    defined(POLARSSL_KEY_EXCHANGE_RSA_PSK_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK )
        ; /* nothing more to do */
    else
#endif /* POLARSSL_KEY_EXCHANGE_PSK_ENABLED ||
          POLARSSL_KEY_EXCHANGE_RSA_PSK_ENABLED */
#if defined(POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED) ||                       \
    defined(POLARSSL_KEY_EXCHANGE_DHE_PSK_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_RSA ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_PSK )
    {
        if( ssl_parse_server_dh_params( ssl, &p, end ) != 0 )
        {
            SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
        }
    }
    else
#endif /* POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_DHE_PSK_ENABLED */
#if defined(POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED) ||                     \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_PSK_ENABLED) ||                     \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_RSA ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA )
    {
        if( ssl_parse_server_ecdh_params( ssl, &p, end ) != 0 )
        {
            SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
        }
    }
    else
#endif /* POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_PSK_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED */
    {
        SSL_DEBUG_MSG( 1, ( "should never happen" ) );
        return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
    }

#if defined(POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED) ||                       \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED) ||                     \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_RSA ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_RSA ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA )
    {
        size_t sig_len, hashlen;
        unsigned char hash[64];
        md_type_t md_alg = POLARSSL_MD_NONE;
        pk_type_t pk_alg = POLARSSL_PK_NONE;
        unsigned char *params = ssl->in_msg + ssl_hs_hdr_len( ssl );
        size_t params_len = p - params;

        /*
         * Handle the digitally-signed structure
         */
#if defined(POLARSSL_SSL_PROTO_TLS1_2)
        if( ssl->minor_ver == SSL_MINOR_VERSION_3 )
        {
            if( ssl_parse_signature_algorithm( ssl, &p, end,
                                               &md_alg, &pk_alg ) != 0 )
            {
                SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
                return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
            }

            if( pk_alg != ssl_get_ciphersuite_sig_pk_alg( ciphersuite_info ) )
            {
                SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
                return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
            }
        }
        else
#endif /* POLARSSL_SSL_PROTO_TLS1_2 */
#if defined(POLARSSL_SSL_PROTO_SSL3) || defined(POLARSSL_SSL_PROTO_TLS1) || \
    defined(POLARSSL_SSL_PROTO_TLS1_1)
        if( ssl->minor_ver < SSL_MINOR_VERSION_3 )
        {
            pk_alg = ssl_get_ciphersuite_sig_pk_alg( ciphersuite_info );

            /* Default hash for ECDSA is SHA-1 */
            if( pk_alg == POLARSSL_PK_ECDSA && md_alg == POLARSSL_MD_NONE )
                md_alg = POLARSSL_MD_SHA1;
        }
        else
#endif
        {
            SSL_DEBUG_MSG( 1, ( "should never happen" ) );
            return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
        }

        /*
         * Read signature
         */
        sig_len = ( p[0] << 8 ) | p[1];
        p += 2;

        if( end != p + sig_len )
        {
            SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_SERVER_KEY_EXCHANGE );
        }

        SSL_DEBUG_BUF( 3, "signature", p, sig_len );

        /*
         * Compute the hash that has been signed
         */
#if defined(POLARSSL_SSL_PROTO_SSL3) || defined(POLARSSL_SSL_PROTO_TLS1) || \
    defined(POLARSSL_SSL_PROTO_TLS1_1)
        if( md_alg == POLARSSL_MD_NONE )
        {
            md5_context md5;
            sha1_context sha1;

            md5_init(  &md5  );
            sha1_init( &sha1 );

            hashlen = 36;

            /*
             * digitally-signed struct {
             *     opaque md5_hash[16];
             *     opaque sha_hash[20];
             * };
             *
             * md5_hash
             *     MD5(ClientHello.random + ServerHello.random
             *                            + ServerParams);
             * sha_hash
             *     SHA(ClientHello.random + ServerHello.random
             *                            + ServerParams);
             */
            md5_starts( &md5 );
            md5_update( &md5, ssl->handshake->randbytes, 64 );
            md5_update( &md5, params, params_len );
            md5_finish( &md5, hash );

            sha1_starts( &sha1 );
            sha1_update( &sha1, ssl->handshake->randbytes, 64 );
            sha1_update( &sha1, params, params_len );
            sha1_finish( &sha1, hash + 16 );

            md5_free(  &md5  );
            sha1_free( &sha1 );
        }
        else
#endif /* POLARSSL_SSL_PROTO_SSL3 || POLARSSL_SSL_PROTO_TLS1 || \
          POLARSSL_SSL_PROTO_TLS1_1 */
#if defined(POLARSSL_SSL_PROTO_TLS1) || defined(POLARSSL_SSL_PROTO_TLS1_1) || \
    defined(POLARSSL_SSL_PROTO_TLS1_2)
        if( md_alg != POLARSSL_MD_NONE )
        {
            md_context_t ctx;

            md_init( &ctx );

            /* Info from md_alg will be used instead */
            hashlen = 0;

            /*
             * digitally-signed struct {
             *     opaque client_random[32];
             *     opaque server_random[32];
             *     ServerDHParams params;
             * };
             */
            if( ( ret = md_init_ctx( &ctx,
                                     md_info_from_type( md_alg ) ) ) != 0 )
            {
                SSL_DEBUG_RET( 1, "md_init_ctx", ret );
                return( ret );
            }

            md_starts( &ctx );
            md_update( &ctx, ssl->handshake->randbytes, 64 );
            md_update( &ctx, params, params_len );
            md_finish( &ctx, hash );
            md_free( &ctx );
        }
        else
#endif /* POLARSSL_SSL_PROTO_TLS1 || POLARSSL_SSL_PROTO_TLS1_1 || \
          POLARSSL_SSL_PROTO_TLS1_2 */
        {
            SSL_DEBUG_MSG( 1, ( "should never happen" ) );
            return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
        }

        SSL_DEBUG_BUF( 3, "parameters hash", hash, hashlen != 0 ? hashlen :
                (unsigned int) ( md_info_from_type( md_alg ) )->size );

        /*
         * Verify signature
         */
        if( ! pk_can_do( &ssl->session_negotiate->peer_cert->pk, pk_alg ) )
        {
            SSL_DEBUG_MSG( 1, ( "bad server key exchange message" ) );
            return( POLARSSL_ERR_SSL_PK_TYPE_MISMATCH );
        }

        if( ( ret = pk_verify( &ssl->session_negotiate->peer_cert->pk,
                               md_alg, hash, hashlen, p, sig_len ) ) != 0 )
        {
            SSL_DEBUG_RET( 1, "pk_verify", ret );
            return( ret );
        }
    }
#endif /* POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED */

exit:
    ssl->state++;

    SSL_DEBUG_MSG( 2, ( "<= parse server key exchange" ) );

    return( 0 );
}

#if !defined(POLARSSL_KEY_EXCHANGE_RSA_ENABLED)       && \
    !defined(POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED)   && \
    !defined(POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED) && \
    !defined(POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED)
static int ssl_parse_certificate_request( ssl_context *ssl )
{
    const ssl_ciphersuite_t *ciphersuite_info = ssl->transform_negotiate->ciphersuite_info;

    SSL_DEBUG_MSG( 2, ( "=> parse certificate request" ) );

    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_PSK )
    {
        SSL_DEBUG_MSG( 2, ( "<= skip parse certificate request" ) );
        ssl->state++;
        return( 0 );
    }

    SSL_DEBUG_MSG( 1, ( "should never happen" ) );
    return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
}
#else
static int ssl_parse_certificate_request( ssl_context *ssl )
{
    int ret;
    unsigned char *buf, *p;
    size_t n = 0, m = 0;
    size_t cert_type_len = 0, dn_len = 0;
    const ssl_ciphersuite_t *ciphersuite_info = ssl->transform_negotiate->ciphersuite_info;

    SSL_DEBUG_MSG( 2, ( "=> parse certificate request" ) );

    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_PSK )
    {
        SSL_DEBUG_MSG( 2, ( "<= skip parse certificate request" ) );
        ssl->state++;
        return( 0 );
    }

    if( ssl->record_read == 0 )
    {
        if( ( ret = ssl_read_record( ssl ) ) != 0 )
        {
            SSL_DEBUG_RET( 1, "ssl_read_record", ret );
            return( ret );
        }

        if( ssl->in_msgtype != SSL_MSG_HANDSHAKE )
        {
            SSL_DEBUG_MSG( 1, ( "bad certificate request message" ) );
            return( POLARSSL_ERR_SSL_UNEXPECTED_MESSAGE );
        }

        ssl->record_read = 1;
    }

    ssl->client_auth = 0;
    ssl->state++;

    if( ssl->in_msg[0] == SSL_HS_CERTIFICATE_REQUEST )
        ssl->client_auth++;

    SSL_DEBUG_MSG( 3, ( "got %s certificate request",
                        ssl->client_auth ? "a" : "no" ) );

    if( ssl->client_auth == 0 )
        goto exit;

    ssl->record_read = 0;

    // TODO: handshake_failure alert for an anonymous server to request
    // client authentication

    /*
     *  struct {
     *      ClientCertificateType certificate_types<1..2^8-1>;
     *      SignatureAndHashAlgorithm
     *        supported_signature_algorithms<2^16-1>; -- TLS 1.2 only
     *      DistinguishedName certificate_authorities<0..2^16-1>;
     *  } CertificateRequest;
     */
    buf = ssl->in_msg;

    // Retrieve cert types
    //
    cert_type_len = buf[ssl_hs_hdr_len( ssl )];
    n = cert_type_len;

    if( ssl->in_hslen < ssl_hs_hdr_len( ssl ) + 2 + n )
    {
        SSL_DEBUG_MSG( 1, ( "bad certificate request message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_CERTIFICATE_REQUEST );
    }

    p = buf + ssl_hs_hdr_len( ssl ) + 1;
    while( cert_type_len > 0 )
    {
#if defined(POLARSSL_RSA_C)
        if( *p == SSL_CERT_TYPE_RSA_SIGN &&
            pk_can_do( ssl_own_key( ssl ), POLARSSL_PK_RSA ) )
        {
            ssl->handshake->cert_type = SSL_CERT_TYPE_RSA_SIGN;
            break;
        }
        else
#endif
#if defined(POLARSSL_ECDSA_C)
        if( *p == SSL_CERT_TYPE_ECDSA_SIGN &&
            pk_can_do( ssl_own_key( ssl ), POLARSSL_PK_ECDSA ) )
        {
            ssl->handshake->cert_type = SSL_CERT_TYPE_ECDSA_SIGN;
            break;
        }
        else
#endif
        {
            ; /* Unsupported cert type, ignore */
        }

        cert_type_len--;
        p++;
    }

#if defined(POLARSSL_SSL_PROTO_TLS1_2)
    if( ssl->minor_ver == SSL_MINOR_VERSION_3 )
    {
        /* Ignored, see comments about hash in write_certificate_verify */
        // TODO: should check the signature part against our pk_key though
        size_t sig_alg_len = ( ( buf[ssl_hs_hdr_len( ssl ) + 1 + n] <<  8 )
                             | ( buf[ssl_hs_hdr_len( ssl ) + 2 + n]       ) );

        p = buf + ssl_hs_hdr_len( ssl ) + 3 + n;
        m += 2;
        n += sig_alg_len;

        if( ssl->in_hslen < ssl_hs_hdr_len( ssl ) + 2 + n )
        {
            SSL_DEBUG_MSG( 1, ( "bad certificate request message" ) );
            return( POLARSSL_ERR_SSL_BAD_HS_CERTIFICATE_REQUEST );
        }
    }
#endif /* POLARSSL_SSL_PROTO_TLS1_2 */

    /* Ignore certificate_authorities, we only have one cert anyway */
    // TODO: should not send cert if no CA matches
    dn_len = ( ( buf[ssl_hs_hdr_len( ssl ) + 1 + m + n] <<  8 )
             | ( buf[ssl_hs_hdr_len( ssl ) + 2 + m + n]       ) );

    n += dn_len;
    if( ssl->in_hslen != ssl_hs_hdr_len( ssl ) + 3 + m + n )
    {
        SSL_DEBUG_MSG( 1, ( "bad certificate request message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_CERTIFICATE_REQUEST );
    }

exit:
    SSL_DEBUG_MSG( 2, ( "<= parse certificate request" ) );

    return( 0 );
}
#endif /* !POLARSSL_KEY_EXCHANGE_RSA_ENABLED &&
          !POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED &&
          !POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED &&
          !POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED */

static int ssl_parse_server_hello_done( ssl_context *ssl )
{
    int ret;

    SSL_DEBUG_MSG( 2, ( "=> parse server hello done" ) );

    if( ssl->record_read == 0 )
    {
        if( ( ret = ssl_read_record( ssl ) ) != 0 )
        {
            SSL_DEBUG_RET( 1, "ssl_read_record", ret );
            return( ret );
        }

        if( ssl->in_msgtype != SSL_MSG_HANDSHAKE )
        {
            SSL_DEBUG_MSG( 1, ( "bad server hello done message" ) );
            return( POLARSSL_ERR_SSL_UNEXPECTED_MESSAGE );
        }
    }
    ssl->record_read = 0;

    if( ssl->in_hslen  != ssl_hs_hdr_len( ssl ) ||
        ssl->in_msg[0] != SSL_HS_SERVER_HELLO_DONE )
    {
        SSL_DEBUG_MSG( 1, ( "bad server hello done message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_SERVER_HELLO_DONE );
    }

    ssl->state++;

#if defined(POLARSSL_SSL_PROTO_DTLS)
    if( ssl->transport == SSL_TRANSPORT_DATAGRAM )
        ssl_recv_flight_completed( ssl );
#endif

    SSL_DEBUG_MSG( 2, ( "<= parse server hello done" ) );

    return( 0 );
}

static int ssl_write_client_key_exchange( ssl_context *ssl )
{
    int ret;
    size_t i, n;
    const ssl_ciphersuite_t *ciphersuite_info = ssl->transform_negotiate->ciphersuite_info;

    SSL_DEBUG_MSG( 2, ( "=> write client key exchange" ) );

#if defined(POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_RSA )
    {
        /*
         * DHM key exchange -- send G^X mod P
         */
        n = ssl->handshake->dhm_ctx.len;

        ssl->out_msg[4] = (unsigned char)( n >> 8 );
        ssl->out_msg[5] = (unsigned char)( n      );
        i = 6;

        ret = dhm_make_public( &ssl->handshake->dhm_ctx,
                                (int) mpi_size( &ssl->handshake->dhm_ctx.P ),
                               &ssl->out_msg[i], n,
                                ssl->f_rng, ssl->p_rng );
        if( ret != 0 )
        {
            SSL_DEBUG_RET( 1, "dhm_make_public", ret );
            return( ret );
        }

        SSL_DEBUG_MPI( 3, "DHM: X ", &ssl->handshake->dhm_ctx.X  );
        SSL_DEBUG_MPI( 3, "DHM: GX", &ssl->handshake->dhm_ctx.GX );

        ssl->handshake->pmslen = POLARSSL_PREMASTER_SIZE;

        if( ( ret = dhm_calc_secret( &ssl->handshake->dhm_ctx,
                                      ssl->handshake->premaster,
                                     &ssl->handshake->pmslen,
                                      ssl->f_rng, ssl->p_rng ) ) != 0 )
        {
            SSL_DEBUG_RET( 1, "dhm_calc_secret", ret );
            return( ret );
        }

        SSL_DEBUG_MPI( 3, "DHM: K ", &ssl->handshake->dhm_ctx.K  );
    }
    else
#endif /* POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED */
#if defined(POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED) ||                     \
    defined(POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED) ||                   \
    defined(POLARSSL_KEY_EXCHANGE_ECDH_RSA_ENABLED) ||                      \
    defined(POLARSSL_KEY_EXCHANGE_ECDH_ECDSA_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_RSA ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDH_RSA ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDH_ECDSA )
    {
        /*
         * ECDH key exchange -- send client public value
         */
        i = 4;

        ret = ecdh_make_public( &ssl->handshake->ecdh_ctx,
                                &n,
                                &ssl->out_msg[i], 1000,
                                ssl->f_rng, ssl->p_rng );
        if( ret != 0 )
        {
            SSL_DEBUG_RET( 1, "ecdh_make_public", ret );
            return( ret );
        }

        SSL_DEBUG_ECP( 3, "ECDH: Q", &ssl->handshake->ecdh_ctx.Q );

        if( ( ret = ecdh_calc_secret( &ssl->handshake->ecdh_ctx,
                                      &ssl->handshake->pmslen,
                                       ssl->handshake->premaster,
                                       POLARSSL_MPI_MAX_SIZE,
                                       ssl->f_rng, ssl->p_rng ) ) != 0 )
        {
            SSL_DEBUG_RET( 1, "ecdh_calc_secret", ret );
            return( ret );
        }

        SSL_DEBUG_MPI( 3, "ECDH: z", &ssl->handshake->ecdh_ctx.z );
    }
    else
#endif /* POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDH_RSA_ENABLED ||
          POLARSSL_KEY_EXCHANGE_ECDH_ECDSA_ENABLED */
#if defined(POLARSSL_KEY_EXCHANGE__SOME__PSK_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_PSK )
    {
        /*
         * opaque psk_identity<0..2^16-1>;
         */
        if( ssl->psk == NULL || ssl->psk_identity == NULL )
            return( POLARSSL_ERR_SSL_PRIVATE_KEY_REQUIRED );

        i = 4;
        n = ssl->psk_identity_len;
        ssl->out_msg[i++] = (unsigned char)( n >> 8 );
        ssl->out_msg[i++] = (unsigned char)( n      );

        memcpy( ssl->out_msg + i, ssl->psk_identity, ssl->psk_identity_len );
        i += ssl->psk_identity_len;

#if defined(POLARSSL_KEY_EXCHANGE_PSK_ENABLED)
        if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK )
        {
            n = 0;
        }
        else
#endif
#if defined(POLARSSL_KEY_EXCHANGE_RSA_PSK_ENABLED)
        if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK )
        {
            if( ( ret = ssl_write_encrypted_pms( ssl, i, &n, 2 ) ) != 0 )
                return( ret );
        }
        else
#endif
#if defined(POLARSSL_KEY_EXCHANGE_DHE_PSK_ENABLED)
        if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_PSK )
        {
            /*
             * ClientDiffieHellmanPublic public (DHM send G^X mod P)
             */
            n = ssl->handshake->dhm_ctx.len;
            ssl->out_msg[i++] = (unsigned char)( n >> 8 );
            ssl->out_msg[i++] = (unsigned char)( n      );

            ret = dhm_make_public( &ssl->handshake->dhm_ctx,
                    (int) mpi_size( &ssl->handshake->dhm_ctx.P ),
                    &ssl->out_msg[i], n,
                    ssl->f_rng, ssl->p_rng );
            if( ret != 0 )
            {
                SSL_DEBUG_RET( 1, "dhm_make_public", ret );
                return( ret );
            }
        }
        else
#endif /* POLARSSL_KEY_EXCHANGE_DHE_PSK_ENABLED */
#if defined(POLARSSL_KEY_EXCHANGE_ECDHE_PSK_ENABLED)
        if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_PSK )
        {
            /*
             * ClientECDiffieHellmanPublic public;
             */
            ret = ecdh_make_public( &ssl->handshake->ecdh_ctx, &n,
                    &ssl->out_msg[i], SSL_MAX_CONTENT_LEN - i,
                    ssl->f_rng, ssl->p_rng );
            if( ret != 0 )
            {
                SSL_DEBUG_RET( 1, "ecdh_make_public", ret );
                return( ret );
            }

            SSL_DEBUG_ECP( 3, "ECDH: Q", &ssl->handshake->ecdh_ctx.Q );
        }
        else
#endif /* POLARSSL_KEY_EXCHANGE_ECDHE_PSK_ENABLED */
        {
            SSL_DEBUG_MSG( 1, ( "should never happen" ) );
            return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
        }

        if( ( ret = ssl_psk_derive_premaster( ssl,
                        ciphersuite_info->key_exchange ) ) != 0 )
        {
            SSL_DEBUG_RET( 1, "ssl_psk_derive_premaster", ret );
            return( ret );
        }
    }
    else
#endif /* POLARSSL_KEY_EXCHANGE__SOME__PSK_ENABLED */
#if defined(POLARSSL_KEY_EXCHANGE_RSA_ENABLED)
    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA )
    {
        i = 4;
        if( ( ret = ssl_write_encrypted_pms( ssl, i, &n, 0 ) ) != 0 )
            return( ret );
    }
    else
#endif /* POLARSSL_KEY_EXCHANGE_RSA_ENABLED */
    {
        ((void) ciphersuite_info);
        SSL_DEBUG_MSG( 1, ( "should never happen" ) );
        return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
    }

    if( ( ret = ssl_derive_keys( ssl ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "ssl_derive_keys", ret );
        return( ret );
    }

    ssl->out_msglen  = i + n;
    ssl->out_msgtype = SSL_MSG_HANDSHAKE;
    ssl->out_msg[0]  = SSL_HS_CLIENT_KEY_EXCHANGE;

    ssl->state++;

    if( ( ret = ssl_write_record( ssl ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "ssl_write_record", ret );
        return( ret );
    }

    SSL_DEBUG_MSG( 2, ( "<= write client key exchange" ) );

    return( 0 );
}

#if !defined(POLARSSL_KEY_EXCHANGE_RSA_ENABLED)       && \
    !defined(POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED)   && \
    !defined(POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED) && \
    !defined(POLARSSL_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED)
static int ssl_write_certificate_verify( ssl_context *ssl )
{
    const ssl_ciphersuite_t *ciphersuite_info = ssl->transform_negotiate->ciphersuite_info;

    SSL_DEBUG_MSG( 2, ( "=> write certificate verify" ) );

    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_PSK )
    {
        SSL_DEBUG_MSG( 2, ( "<= skip write certificate verify" ) );
        ssl->state++;
        return( 0 );
    }

    SSL_DEBUG_MSG( 1, ( "should never happen" ) );
    return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
}
#else
static int ssl_write_certificate_verify( ssl_context *ssl )
{
    int ret = POLARSSL_ERR_SSL_FEATURE_UNAVAILABLE;
    const ssl_ciphersuite_t *ciphersuite_info = ssl->transform_negotiate->ciphersuite_info;
    size_t n = 0, offset = 0;
    unsigned char hash[48];
    unsigned char *hash_start = hash;
    md_type_t md_alg = POLARSSL_MD_NONE;
    unsigned int hashlen;

    SSL_DEBUG_MSG( 2, ( "=> write certificate verify" ) );

    if( ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_RSA_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_ECDHE_PSK ||
        ciphersuite_info->key_exchange == POLARSSL_KEY_EXCHANGE_DHE_PSK )
    {
        SSL_DEBUG_MSG( 2, ( "<= skip write certificate verify" ) );
        ssl->state++;
        return( 0 );
    }

    if( ssl->client_auth == 0 || ssl_own_cert( ssl ) == NULL )
    {
        SSL_DEBUG_MSG( 2, ( "<= skip write certificate verify" ) );
        ssl->state++;
        return( 0 );
    }

    if( ssl_own_key( ssl ) == NULL )
    {
        SSL_DEBUG_MSG( 1, ( "got no private key" ) );
        return( POLARSSL_ERR_SSL_PRIVATE_KEY_REQUIRED );
    }

    /*
     * Make an RSA signature of the handshake digests
     */
    ssl->handshake->calc_verify( ssl, hash );

#if defined(POLARSSL_SSL_PROTO_SSL3) || defined(POLARSSL_SSL_PROTO_TLS1) || \
    defined(POLARSSL_SSL_PROTO_TLS1_1)
    if( ssl->minor_ver != SSL_MINOR_VERSION_3 )
    {
        /*
         * digitally-signed struct {
         *     opaque md5_hash[16];
         *     opaque sha_hash[20];
         * };
         *
         * md5_hash
         *     MD5(handshake_messages);
         *
         * sha_hash
         *     SHA(handshake_messages);
         */
        hashlen = 36;
        md_alg = POLARSSL_MD_NONE;

        /*
         * For ECDSA, default hash is SHA-1 only
         */
        if( pk_can_do( ssl_own_key( ssl ), POLARSSL_PK_ECDSA ) )
        {
            hash_start += 16;
            hashlen -= 16;
            md_alg = POLARSSL_MD_SHA1;
        }
    }
    else
#endif /* POLARSSL_SSL_PROTO_SSL3 || POLARSSL_SSL_PROTO_TLS1 || \
          POLARSSL_SSL_PROTO_TLS1_1 */
#if defined(POLARSSL_SSL_PROTO_TLS1_2)
    if( ssl->minor_ver == SSL_MINOR_VERSION_3 )
    {
        /*
         * digitally-signed struct {
         *     opaque handshake_messages[handshake_messages_length];
         * };
         *
         * Taking shortcut here. We assume that the server always allows the
         * PRF Hash function and has sent it in the allowed signature
         * algorithms list received in the Certificate Request message.
         *
         * Until we encounter a server that does not, we will take this
         * shortcut.
         *
         * Reason: Otherwise we should have running hashes for SHA512 and SHA224
         *         in order to satisfy 'weird' needs from the server side.
         */
        if( ssl->transform_negotiate->ciphersuite_info->mac ==
            POLARSSL_MD_SHA384 )
        {
            md_alg = POLARSSL_MD_SHA384;
            ssl->out_msg[4] = SSL_HASH_SHA384;
        }
        else
        {
            md_alg = POLARSSL_MD_SHA256;
            ssl->out_msg[4] = SSL_HASH_SHA256;
        }
        ssl->out_msg[5] = ssl_sig_from_pk( ssl_own_key( ssl ) );

        /* Info from md_alg will be used instead */
        hashlen = 0;
        offset = 2;
    }
    else
#endif /* POLARSSL_SSL_PROTO_TLS1_2 */
    {
        SSL_DEBUG_MSG( 1, ( "should never happen" ) );
        return( POLARSSL_ERR_SSL_INTERNAL_ERROR );
    }

    if( ( ret = pk_sign( ssl_own_key( ssl ), md_alg, hash_start, hashlen,
                         ssl->out_msg + 6 + offset, &n,
                         ssl->f_rng, ssl->p_rng ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "pk_sign", ret );
        return( ret );
    }

    ssl->out_msg[4 + offset] = (unsigned char)( n >> 8 );
    ssl->out_msg[5 + offset] = (unsigned char)( n      );

    ssl->out_msglen  = 6 + n + offset;
    ssl->out_msgtype = SSL_MSG_HANDSHAKE;
    ssl->out_msg[0]  = SSL_HS_CERTIFICATE_VERIFY;

    ssl->state++;

    if( ( ret = ssl_write_record( ssl ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "ssl_write_record", ret );
        return( ret );
    }

    SSL_DEBUG_MSG( 2, ( "<= write certificate verify" ) );

    return( ret );
}
#endif /* !POLARSSL_KEY_EXCHANGE_RSA_ENABLED &&
          !POLARSSL_KEY_EXCHANGE_DHE_RSA_ENABLED &&
          !POLARSSL_KEY_EXCHANGE_ECDHE_RSA_ENABLED */

#if defined(POLARSSL_SSL_SESSION_TICKETS)
static int ssl_parse_new_session_ticket( ssl_context *ssl )
{
    int ret;
    uint32_t lifetime;
    size_t ticket_len;
    unsigned char *ticket;
    const unsigned char *msg;

    SSL_DEBUG_MSG( 2, ( "=> parse new session ticket" ) );

    if( ( ret = ssl_read_record( ssl ) ) != 0 )
    {
        SSL_DEBUG_RET( 1, "ssl_read_record", ret );
        return( ret );
    }

    if( ssl->in_msgtype != SSL_MSG_HANDSHAKE )
    {
        SSL_DEBUG_MSG( 1, ( "bad new session ticket message" ) );
        return( POLARSSL_ERR_SSL_UNEXPECTED_MESSAGE );
    }

    /*
     * struct {
     *     uint32 ticket_lifetime_hint;
     *     opaque ticket<0..2^16-1>;
     * } NewSessionTicket;
     *
     * 0  .  3   ticket_lifetime_hint
     * 4  .  5   ticket_len (n)
     * 6  .  5+n ticket content
     */
    if( ssl->in_msg[0] != SSL_HS_NEW_SESSION_TICKET ||
        ssl->in_hslen < 6 + ssl_hs_hdr_len( ssl ) )
    {
        SSL_DEBUG_MSG( 1, ( "bad new session ticket message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_NEW_SESSION_TICKET );
    }

    msg = ssl->in_msg + ssl_hs_hdr_len( ssl );

    lifetime = ( msg[0] << 24 ) | ( msg[1] << 16 ) |
               ( msg[2] <<  8 ) | ( msg[3]       );

    ticket_len = ( msg[4] << 8 ) | ( msg[5] );

    if( ticket_len + 6 + ssl_hs_hdr_len( ssl ) != ssl->in_hslen )
    {
        SSL_DEBUG_MSG( 1, ( "bad new session ticket message" ) );
        return( POLARSSL_ERR_SSL_BAD_HS_NEW_SESSION_TICKET );
    }

    SSL_DEBUG_MSG( 3, ( "ticket length: %d", ticket_len ) );

    /* We're not waiting for a NewSessionTicket message any more */
    ssl->handshake->new_session_ticket = 0;
    ssl->state = SSL_SERVER_CHANGE_CIPHER_SPEC;

    /*
     * Zero-length ticket means the server changed his mind and doesn't want
     * to send a ticket after all, so just forget it
     */
    if( ticket_len == 0 )
        return( 0 );

    polarssl_zeroize( ssl->session_negotiate->ticket,
                      ssl->session_negotiate->ticket_len );
    polarssl_free( ssl->session_negotiate->ticket );
    ssl->session_negotiate->ticket = NULL;
    ssl->session_negotiate->ticket_len = 0;

    if( ( ticket = polarssl_malloc( ticket_len ) ) == NULL )
    {
        SSL_DEBUG_MSG( 1, ( "ticket malloc failed" ) );
        return( POLARSSL_ERR_SSL_MALLOC_FAILED );
    }

    memcpy( ticket, msg + 6, ticket_len );

    ssl->session_negotiate->ticket = ticket;
    ssl->session_negotiate->ticket_len = ticket_len;
    ssl->session_negotiate->ticket_lifetime = lifetime;

    /*
     * RFC 5077 section 3.4:
     * "If the client receives a session ticket from the server, then it
     * discards any Session ID that was sent in the ServerHello."
     */
    SSL_DEBUG_MSG( 3, ( "ticket in use, discarding session id" ) );
    ssl->session_negotiate->length = 0;

    SSL_DEBUG_MSG( 2, ( "<= parse new session ticket" ) );

    return( 0 );
}
#endif /* POLARSSL_SSL_SESSION_TICKETS */

/*
 * SSL handshake -- client side -- single step
 */
int ssl_handshake_client_step( ssl_context *ssl )
{
    int ret = 0;

    if( ssl->state == SSL_HANDSHAKE_OVER )
        return( POLARSSL_ERR_SSL_BAD_INPUT_DATA );

    SSL_DEBUG_MSG( 2, ( "client state: %d", ssl->state ) );

    if( ( ret = ssl_flush_output( ssl ) ) != 0 )
        return( ret );

#if defined(POLARSSL_SSL_PROTO_DTLS)
    if( ssl->transport == SSL_TRANSPORT_DATAGRAM &&
        ssl->handshake != NULL &&
        ssl->handshake->retransmit_state == SSL_RETRANS_SENDING )
    {
        if( ( ret = ssl_resend( ssl ) ) != 0 )
            return( ret );
    }
#endif

    /* Change state now, so that it is right in ssl_read_record(), used
     * by DTLS for dropping out-of-sequence ChangeCipherSpec records */
#if defined(POLARSSL_SSL_SESSION_TICKETS)
    if( ssl->state == SSL_SERVER_CHANGE_CIPHER_SPEC &&
        ssl->handshake->new_session_ticket != 0 )
    {
        ssl->state = SSL_SERVER_NEW_SESSION_TICKET;
    }
#endif

    switch( ssl->state )
    {
        case SSL_HELLO_REQUEST:
            ssl->state = SSL_CLIENT_HELLO;
            break;

       /*
        *  ==>   ClientHello
        */
       case SSL_CLIENT_HELLO:
           ret = ssl_write_client_hello( ssl );
           break;

       /*
        *  <==   ServerHello
        *        Certificate
        *      ( ServerKeyExchange  )
        *      ( CertificateRequest )
        *        ServerHelloDone
        */
       case SSL_SERVER_HELLO:
           ret = ssl_parse_server_hello( ssl );
           break;

       case SSL_SERVER_CERTIFICATE:
           ret = ssl_parse_certificate( ssl );
           break;

       case SSL_SERVER_KEY_EXCHANGE:
           ret = ssl_parse_server_key_exchange( ssl );
           break;

       case SSL_CERTIFICATE_REQUEST:
           ret = ssl_parse_certificate_request( ssl );
           break;

       case SSL_SERVER_HELLO_DONE:
           ret = ssl_parse_server_hello_done( ssl );
           break;

       /*
        *  ==> ( Certificate/Alert  )
        *        ClientKeyExchange
        *      ( CertificateVerify  )
        *        ChangeCipherSpec
        *        Finished
        */
       case SSL_CLIENT_CERTIFICATE:
           ret = ssl_write_certificate( ssl );
           break;

       case SSL_CLIENT_KEY_EXCHANGE:
           ret = ssl_write_client_key_exchange( ssl );
           break;

       case SSL_CERTIFICATE_VERIFY:
           ret = ssl_write_certificate_verify( ssl );
           break;

       case SSL_CLIENT_CHANGE_CIPHER_SPEC:
           ret = ssl_write_change_cipher_spec( ssl );
           break;

       case SSL_CLIENT_FINISHED:
           ret = ssl_write_finished( ssl );
           break;

       /*
        *  <==   ( NewSessionTicket )
        *        ChangeCipherSpec
        *        Finished
        */
#if defined(POLARSSL_SSL_SESSION_TICKETS)
       case SSL_SERVER_NEW_SESSION_TICKET:
           ret = ssl_parse_new_session_ticket( ssl );
           break;
#endif

       case SSL_SERVER_CHANGE_CIPHER_SPEC:
           ret = ssl_parse_change_cipher_spec( ssl );
           break;

       case SSL_SERVER_FINISHED:
           ret = ssl_parse_finished( ssl );
           break;

       case SSL_FLUSH_BUFFERS:
           SSL_DEBUG_MSG( 2, ( "handshake: done" ) );
           ssl->state = SSL_HANDSHAKE_WRAPUP;
           break;

       case SSL_HANDSHAKE_WRAPUP:
           ssl_handshake_wrapup( ssl );
           break;

       default:
           SSL_DEBUG_MSG( 1, ( "invalid state %d", ssl->state ) );
           return( POLARSSL_ERR_SSL_BAD_INPUT_DATA );
   }

    return( ret );
}
#endif /* POLARSSL_SSL_CLI_C */
