#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_i_gsi_callback.h
 * Globus GSI Callback
 * @author Sam Meder, Sam Lang
 *
 * $RCSfile$
 * $Revision$
 * $Date$
 */

#ifndef _GLOBUS_I_GSI_CALLBACK_H_
#define _GLOBUS_I_GSI_CALLBACK_H_

#include "globus_gsi_callback.h"
#include "globus_gsi_cert_utils.h"

/* DEBUG MACROS */

#ifdef BUILD_DEBUG

extern int                              globus_i_gsi_callback_debug_level;
extern FILE *                           globus_i_gsi_callback_debug_fstream;

#define GLOBUS_I_GSI_CALLBACK_DEBUG(_LEVEL_) \
    (globus_i_gsi_callback_debug_level >= (_LEVEL_))

#define GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF(_LEVEL_, _MESSAGE_) \
    { \
        if (GLOBUS_I_GSI_CALLBACK_DEBUG(_LEVEL_)) \
        { \
           globus_libc_fprintf _MESSAGE_; \
        } \
    }

#define GLOBUS_I_GSI_CALLBACK_DEBUG_FNPRINTF(_LEVEL_, _MESSAGE_) \
    { \
        if (GLOBUS_I_GSI_CALLBACK_DEBUG(_LEVEL_)) \
        { \
           char *                          _tmp_str_ = \
               globus_gsi_cert_utils_create_nstring _MESSAGE_; \
           globus_libc_fprintf(globus_i_gsi_callback_debug_fstream, \
                               _tmp_str_); \
           globus_libc_free(_tmp_str_); \
        } \
    }

#define GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT(_LEVEL_, _MESSAGE_) \
    { \
        if (GLOBUS_I_GSI_CALLBACK_DEBUG(_LEVEL_)) \
        { \
           globus_libc_fprintf(globus_i_gsi_callback_debug_fstream, \
                               _MESSAGE_); \
        } \
    }

#define GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT_OBJECT(_LEVEL_, _OBJ_NAME_, _OBJ_) \
    { \
        if (GLOBUS_I_GSI_CALLBACK_DEBUG(_LEVEL_)) \
        { \
           _OBJ_NAME_##_print_fp(globus_i_gsi_callback_debug_fstream, _OBJ_); \
        } \
    }

#else

#define GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF(_LEVEL_, _MESSAGE_) {}
#define GLOBUS_I_GSI_CALLBACK_DEBUG_FNPRINTF(_LEVEL_, _MESSAGE_) {}
#define GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT(_LEVEL_, _MESSAGE_) {}
#define GLOBUS_I_GSI_CALLBACK_DEBUG_PRINT_OBJECT(_LEVEL_, _OBJ_NAME_, _OBJ_) {}

#endif
         
#define GLOBUS_I_GSI_CALLBACK_DEBUG_ENTER \
            GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF( \
                1, (globus_i_gsi_callback_debug_fstream, \
                    "%s entering\n", _function_name_))

#define GLOBUS_I_GSI_CALLBACK_DEBUG_EXIT \
            GLOBUS_I_GSI_CALLBACK_DEBUG_FPRINTF( \
                2, (globus_i_gsi_callback_debug_fstream, \
                    "%s exiting\n", _function_name_))

/* ERROR MACROS */

#define GLOBUS_GSI_CALLBACK_OPENSSL_ERROR_RESULT(_RESULT_,     \
                                                 _ERRORTYPE_,  \
                                                 _ERRSTR_)     \
    {                                                          \
        char *                          _tmp_str_ =            \
            globus_gsi_cert_utils_create_string _ERRSTR_;      \
        _RESULT_ = globus_i_gsi_callback_openssl_error_result( \
                       _ERRORTYPE_,                            \
                       __FILE__,                               \
                       _function_name_,                        \
                       __LINE__,                               \
                       _tmp_str_);                             \
        globus_libc_free(_tmp_str_);                           \
    }

#define GLOBUS_GSI_CALLBACK_ERROR_RESULT(_RESULT_, _ERRORTYPE_, _ERRSTR_) \
    {                                                                     \
        char *                          _tmp_str_ =                       \
            globus_gsi_cert_utils_create_string _ERRSTR_;                 \
        _RESULT_ = globus_i_gsi_callback_error_result(_ERRORTYPE_,        \
                                                  __FILE__,               \
                                                  _function_name_,        \
                                                  __LINE__,               \
                                                  _tmp_str_);             \
        globus_libc_free(_tmp_str_);                                      \
    }

#define GLOBUS_GSI_CALLBACK_ERROR_CHAIN_RESULT(_TOP_RESULT_, _ERRORTYPE_) \
    _TOP_RESULT_ = globus_i_gsi_callback_error_chain_result(              \
                       _TOP_RESULT_,                                      \
                       _ERRORTYPE_,                                       \
                       __FILE__,                                          \
                       _function_name_,                                   \
                       __LINE__,                                          \
                       NULL)

extern char *                    globus_l_gsi_callback_error_strings[];

/**
 * GSI Credential Callback implementation
 * @ingroup globus_gsi_callback_data
 * @internal
 *
 * This structure contains parameters set during
 */
typedef struct globus_l_gsi_callback_data_s {    

    int                                 cert_depth;
    int                                 proxy_depth;
    globus_gsi_cert_utils_proxy_type_t  proxy_type;
    STACK_OF(X509) *                    cert_chain;
    int                                 multiple_limited_proxy_ok;
    char *                              cert_dir;
    globus_gsi_extension_callback_t     extension_cb;
    void *                              extension_oids;
    globus_result_t                     error;

} globus_i_gsi_callback_data_t;

globus_result_t
globus_i_gsi_callback_check_path_length(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data);

globus_result_t
globus_i_gsi_callback_check_critical_extensions(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data);

globus_result_t
globus_i_gsi_callback_check_signing_policy(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data);

globus_result_t
globus_i_gsi_callback_check_revoked(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data);

globus_result_t
globus_i_gsi_callback_check_proxy(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data);

globus_result_t
globus_i_gsi_callback_check_gaa_auth(
    X509_STORE_CTX *                    x509_context,
    globus_gsi_callback_data_t          callback_data);

globus_result_t
globus_i_gsi_callback_cred_verify(
    int                                 preverify_ok,
    globus_gsi_callback_data_t          callback_data,
    X509_STORE_CTX *                    x509_context);

globus_result_t
globus_i_gsi_callback_openssl_error_result(
    int                                 error_type,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        long_desc);

globus_result_t
globus_i_gsi_callback_error_result(
    int                                 error_type,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        long_desc);

globus_result_t
globus_i_gsi_callback_error_chain_result(
    globus_result_t                     chain_result,
    int                                 error_type,
    const char *                        filename,
    const char *                        function_name,
    int                                 line_number,
    const char *                        long_desc);

#endif /* _GLOBUS_I_GSI_CALLBACK_H_ */

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
