#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_ftp_client_perf_plugin.c GridFTP Performance Marker Plugin Implementation
 *
 * $RCSfile$
 * $Revision$
 * $Date$
 * $Author$
 */

#include "globus_ftp_client_perf_plugin.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#define GLOBUS_L_FTP_CLIENT_PERF_PLUGIN_NAME "globus_ftp_client_perf_plugin"

static globus_bool_t globus_l_ftp_client_perf_plugin_activate(void);
static globus_bool_t globus_l_ftp_client_perf_plugin_deactivate(void);

globus_module_descriptor_t globus_i_ftp_client_perf_plugin_module =
{
    GLOBUS_L_FTP_CLIENT_PERF_PLUGIN_NAME,
    globus_l_ftp_client_perf_plugin_activate,
    globus_l_ftp_client_perf_plugin_deactivate,
    GLOBUS_NULL
};

static
int
globus_l_ftp_client_perf_plugin_activate(void)
{
    int rc;

    rc = globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    return rc;
}

static
int
globus_l_ftp_client_perf_plugin_deactivate(void)
{
    return globus_module_deactivate(GLOBUS_FTP_CLIENT_MODULE);
}

typedef struct perf_plugin_info_s
{
    void *                                          user_specific;
    globus_ftp_client_perf_plugin_begin_cb_t        begin_cb;
    globus_ftp_client_perf_plugin_marker_cb_t       marker_cb;
    globus_ftp_client_perf_plugin_complete_cb_t     complete_cb;
    globus_ftp_client_perf_plugin_user_copy_cb_t    copy_cb;
    globus_ftp_client_perf_plugin_user_destroy_cb_t destroy_cb;

    /* used for get command or put (when put not in EB mode) only */
    globus_bool_t                                   use_data;
    time_t                                          last_time;
    globus_size_t                                   nbytes;
    globus_mutex_t                                  lock;

} perf_plugin_info_t;


/**
 * Plugin complete callback
 * @ingroup globus_ftp_client_perf_plugin
 *
 * This callback will be called when one of the transfer commands
 * is completed. Will also call user's 'complete' callback
 */

static
void
perf_plugin_complete_cb(
    globus_ftp_client_plugin_t *                plugin,
    void *                                      plugin_specific,
    globus_ftp_client_handle_t *                handle)
{
    perf_plugin_info_t *                        ps;

    ps = (perf_plugin_info_t *) plugin_specific;

    if(ps->complete_cb)
    {
        ps->complete_cb(
            handle,
            ps->user_specific);
    }
}

/**
 * Plugin response callback
 * @ingroup globus_ftp_client_perf_plugin
 *
 * Parses out performance markers and calls user's 'marker' callback.
 */

static
void
perf_plugin_response_cb(
    globus_ftp_client_plugin_t *                plugin,
    void *                                      plugin_specific,
    globus_ftp_client_handle_t *                handle,
    const char *                                url,
    globus_object_t *                           error,
    const globus_ftp_control_response_t *       ftp_response)
{
    perf_plugin_info_t *                        ps;
    char *                                      buffer;
    char *                                      tmp_ptr;
    int                                         count;
    time_t                                      time_stamp;
    int                                         stripe_ndx;
    int                                         num_stripes;
    globus_size_t                               nbytes;

    ps = (perf_plugin_info_t *) plugin_specific;

    if(ps->marker_cb &&
        ftp_response &&
        ftp_response->response_buffer &&
        ftp_response->code == 112 &&
        !ps->use_data) /* bad server... sending markers in non-EB mode */
    {
        buffer = (char *) ftp_response->response_buffer;

        /* parse out time stamp */
        tmp_ptr = strstr(buffer, "Timestamp:");
        if(tmp_ptr == NULL)
        {
            return;
        }
        count = sscanf(tmp_ptr + sizeof("Timestamp:"),
            " %ld ", &time_stamp);
        if(count != 1)
        {
            return;
        }

        /* parse out Stripe Index */
        tmp_ptr = strstr(buffer, "Stripe Index:");
        if(tmp_ptr == NULL)
        {
            return;
        }
        count = sscanf(tmp_ptr + sizeof("Stripe Index:"),
            " %d ", &stripe_ndx);
        if(count != 1)
        {
            return;
        }

        /* parse out total stripe count */
        tmp_ptr = strstr(buffer, "Total Stripe Count:");
        if(tmp_ptr == NULL)
        {
            return;
        }
        count = sscanf(tmp_ptr + sizeof("Total Stripe Count:"),
            " %d ", &num_stripes);
        if(count != 1)
        {
            return;
        }

        /* parse out bytes transfered */
        tmp_ptr = strstr(buffer, "Stripe Bytes Transferred:");
        if(tmp_ptr == NULL)
        {
            return;
        }
        count = sscanf(tmp_ptr + sizeof("Stripe Bytes Transferred:"),
            " %d ", &nbytes);
        if(count != 1)
        {
            return;
        }

        ps->marker_cb(
            handle,
            ps->user_specific,
            time_stamp,
            stripe_ndx,
            num_stripes,
            nbytes);
    }
}

/**
 * Plugin data callback
 * @ingroup globus_ftp_client_perf_plugin
 *
 * This plugin is used to create performance marker callbacks in the case
 * of a 'get' command.  (no performance markers are received in the 'get'
 * case)
 */

static
void
perf_plugin_data_cb(
    globus_ftp_client_plugin_t *                plugin,
    void *                                      plugin_specific,
    globus_ftp_client_handle_t *                handle,
    globus_object_t *                           error,
    const globus_byte_t *                       buffer,
    globus_size_t                               length,
    globus_off_t                                offset,
    globus_bool_t                               eof)
{
    perf_plugin_info_t *                        ps;
    time_t                                      time_now;

    ps = (perf_plugin_info_t *) plugin_specific;

    if(ps->use_data && !error)
    {
        globus_mutex_lock(&ps->lock);

        time_now = time(NULL);
        ps->nbytes += length;

        if(ps->marker_cb && time_now > ps->last_time)
        {
            ps->last_time = time_now;

            ps->marker_cb(
                handle,
                ps->user_specific,
                time_now,
                0,
                1,
                ps->nbytes);
        }

        globus_mutex_unlock(&ps->lock);
    }
}

/**
 * Plugin get command callback
 * @ingroup globus_ftp_client_perf_plugin
 *
 * This callback signifies the start of a 'get' command. It will
 * call the user's 'begin' callback
 */

static
void
perf_plugin_get_cb(
    globus_ftp_client_plugin_t *                plugin,
    void *                                      plugin_specific,
    globus_ftp_client_handle_t *                handle,
    const char *                                url,
    const globus_ftp_client_operationattr_t *   attr,
    globus_bool_t                               restart)
{
    perf_plugin_info_t *                        ps;

    if(restart)
    {
        return;
    }

    ps = (perf_plugin_info_t *) plugin_specific;

    ps->use_data = GLOBUS_TRUE;
    ps->nbytes = 0;
    ps->last_time = 0;

    if(ps->begin_cb)
    {
        ps->begin_cb(
            handle,
            ps->user_specific);
    }
}


/**
 * Plugin put command callback
 * @ingroup globus_ftp_client_perf_plugin
 *
 * This callback signifies the start of a 'put' command. It will
 * call the user's 'begin' callback
 */

static
void
perf_plugin_put_cb(
    globus_ftp_client_plugin_t *                plugin,
    void *                                      plugin_specific,
    globus_ftp_client_handle_t *                handle,
    const char *                                url,
    const globus_ftp_client_operationattr_t *   attr,
    globus_bool_t                               restart)
{
    perf_plugin_info_t *                        ps;
    globus_ftp_control_mode_t                   mode;
    globus_result_t                             result;

    if(restart)
    {
        return;
    }

    ps = (perf_plugin_info_t *) plugin_specific;

    result = globus_ftp_client_operationattr_get_mode(attr, &mode);
    if(result == GLOBUS_SUCCESS &&
        mode == GLOBUS_FTP_CONTROL_MODE_EXTENDED_BLOCK)
    {
        ps->use_data = GLOBUS_FALSE;
    }
    else
    {
        ps->use_data = GLOBUS_TRUE;
        ps->nbytes = 0;
        ps->last_time = 0;
    }

    if(ps->begin_cb)
    {
        ps->begin_cb(
            handle,
            ps->user_specific);
    }
}


/**
 * Plugin thrid party transfer command callback
 * @ingroup globus_ftp_client_perf_plugin
 *
 * This callback signifies the start of a 'transfer' command. It will
 * call the user's 'begin' callback
 */

static
void
perf_plugin_transfer_cb(
    globus_ftp_client_plugin_t *                plugin,
    void *                                      plugin_specific,
    globus_ftp_client_handle_t *                handle,
    const char *                                source_url,
    const globus_ftp_client_operationattr_t *   source_attr,
    const char *                                dest_url,
    const globus_ftp_client_operationattr_t *   dest_attr,
    globus_bool_t                               restart)
{
    perf_plugin_info_t *                        ps;

    if(restart)
    {
        return;
    }

    ps = (perf_plugin_info_t *) plugin_specific;

    ps->use_data = GLOBUS_FALSE;

    if(ps->begin_cb)
    {
        ps->begin_cb(
            handle,
            ps->user_specific);
    }
}

/**
 * Plugin copy callback
 * @ingroup globus_ftp_client_perf_plugin
 *
 * This callback is called to create a new copy of this plugin. It will also
 * call the user's 'copy' callback
 */

static
globus_ftp_client_plugin_t *
perf_plugin_copy_cb(
    globus_ftp_client_plugin_t *                plugin_template,
    void *                                      plugin_specific)
{
    perf_plugin_info_t *                        old_ps;
    globus_ftp_client_plugin_t *                new_plugin;
    void *                                      new_user_specific;
    globus_result_t                             result;

    old_ps = (perf_plugin_info_t *) plugin_specific;

    new_plugin = (globus_ftp_client_plugin_t *)
        globus_malloc(sizeof(globus_ftp_client_plugin_t));

    if(new_plugin == GLOBUS_NULL)
    {
        return GLOBUS_NULL;
    }

    if(old_ps->copy_cb)
    {
        new_user_specific = old_ps->copy_cb(old_ps->user_specific);
    }
    else
    {
        new_user_specific = old_ps->user_specific;
    }

    result = globus_ftp_client_perf_plugin_init(
        new_plugin,
        old_ps->begin_cb,
        old_ps->marker_cb,
        old_ps->complete_cb,
        old_ps->copy_cb,
        old_ps->destroy_cb,
        new_user_specific);

    if(result != GLOBUS_SUCCESS)
    {
        globus_free(new_plugin);
        if(old_ps->copy_cb && old_ps->destroy_cb)
        {
            old_ps->destroy_cb(new_user_specific);
        }

        return GLOBUS_NULL;
    }

    return new_plugin;
}

/**
 * Plugin destroy callback
 * @ingroup globus_ftp_client_perf_plugin
 *
 * This callback is called to destroy a copy of a plugin made with the
 * copy callback above.  It will also call the user's 'destroy' callback
 * iff the user has registered a 'copy' callback
 */

static
void
perf_plugin_destroy_cb(
    globus_ftp_client_plugin_t *                plugin,
    void *                                      plugin_specific)
{
    perf_plugin_info_t *                        ps;

    ps = (perf_plugin_info_t *) plugin_specific;

    if(ps->copy_cb && ps->destroy_cb)
    {
        ps->destroy_cb(ps->user_specific);
    }

    globus_ftp_client_perf_plugin_destroy(plugin);
    globus_free(plugin);
}

#endif  /* GLOBUS_DONT_DOCUMENT_INTERNAL */

/**
 * Initialize a perf plugin
 * @ingroup globus_ftp_client_perf_plugin
 *
 * This function initializes a performance marker plugin. Any params
 * except for the plugin may be GLOBUS_NULL
 *
 * @param plugin
 *        a pointer to a plugin type to be initialized
 *
 * @param user_specific
 *        a pointer to some user specific data that will be provided to
 *        all callbacks
 *
 * @param begin_cb
 *        the callback to be called upon the start of a transfer
 *
 * @param marker_cb
 *        the callback to be called with every performance marker received
 *
 * @param complete_cb
 *        the callback to be called to indicate transfer completion
 *
 * @param copy_cb
 *        a copy of this plugin is automatically made for every handle
 *        that uses it.  This callback will be called to allow
 *        a user to create new state data to accompany this copy of the
 *        plugin
 *
 * @param destroy_cb
 *        the callback to be called upon destruction of a copy of the plugin
 *        (Note: will only be called if a copy_cb is also registered)
 *
 * @return
 *        - GLOBUS_SUCCESS
 *        - Error on NULL plugin
 *        - Error on init internal plugin
 */

globus_result_t
globus_ftp_client_perf_plugin_init(
    globus_ftp_client_plugin_t *                    plugin,
    globus_ftp_client_perf_plugin_begin_cb_t        begin_cb,
    globus_ftp_client_perf_plugin_marker_cb_t       marker_cb,
    globus_ftp_client_perf_plugin_complete_cb_t     complete_cb,
    globus_ftp_client_perf_plugin_user_copy_cb_t    copy_cb,
    globus_ftp_client_perf_plugin_user_destroy_cb_t destroy_cb,
    void *                                          user_specific)
{
    perf_plugin_info_t *                            ps;
    globus_result_t                                 result;
    static char *                                   myname =
        "globus_ftp_client_perf_plugin_init";

    if(plugin == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
            GLOBUS_FTP_CLIENT_MODULE,
            GLOBUS_NULL,
            "[%s] NULL plugin at %s\n",
            GLOBUS_FTP_CLIENT_MODULE->module_name,
            myname));
    }

    ps = (perf_plugin_info_t *)
        globus_malloc(sizeof(perf_plugin_info_t));

    if(ps == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
            GLOBUS_FTP_CLIENT_MODULE,
            GLOBUS_NULL,
            "[%s] Out of memory at %s\n",
             GLOBUS_FTP_CLIENT_MODULE->module_name,
             myname));
    }

    /*
     *  initialize plugin specific structure.
     */
    ps->user_specific       = user_specific;
    ps->begin_cb            = begin_cb;
    ps->marker_cb           = marker_cb;
    ps->complete_cb         = complete_cb;
    ps->copy_cb             = copy_cb;
    ps->destroy_cb          = destroy_cb;

    globus_mutex_init(&ps->lock, GLOBUS_NULL);

    result = globus_ftp_client_plugin_init(
              plugin,
              GLOBUS_L_FTP_CLIENT_PERF_PLUGIN_NAME,
              GLOBUS_FTP_CLIENT_CMD_MASK_FILE_ACTIONS,
              ps);

    if(result != GLOBUS_SUCCESS)
    {
        globus_mutex_destroy(&ps->lock);
        globus_free(ps);

        return result;
    }

    globus_ftp_client_plugin_set_destroy_func(plugin,
        perf_plugin_destroy_cb);
    globus_ftp_client_plugin_set_copy_func(plugin,
        perf_plugin_copy_cb);
    globus_ftp_client_plugin_set_get_func(plugin,
        perf_plugin_get_cb);
    globus_ftp_client_plugin_set_data_func(plugin,
        perf_plugin_data_cb);
    globus_ftp_client_plugin_set_put_func(plugin,
        perf_plugin_put_cb);
    globus_ftp_client_plugin_set_third_party_transfer_func(plugin,
        perf_plugin_transfer_cb);
    globus_ftp_client_plugin_set_response_func(plugin,
        perf_plugin_response_cb);
    globus_ftp_client_plugin_set_complete_func(plugin,
        perf_plugin_complete_cb);

    return GLOBUS_SUCCESS;
}

/**
 * Destroy performance marker plugin
 * @ingroup globus_ftp_client_perf_plugin
 *
 * Frees up memory associated with plugin
 *
 * @param plugin
 *        plugin previously initialized with init (above)
 *
 * @return
 *        - GLOBUS_SUCCESS
 *        - Error on NULL plugin
 */

globus_result_t
globus_ftp_client_perf_plugin_destroy(
    globus_ftp_client_plugin_t *                    plugin)
{
    globus_result_t                                 result;
    perf_plugin_info_t *                            ps;
    static char *                                   myname =
        "globus_ftp_client_perf_plugin_destroy";

    if(plugin == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
            GLOBUS_FTP_CLIENT_MODULE,
            GLOBUS_NULL,
            "[%s] NULL plugin at %s\n",
            GLOBUS_FTP_CLIENT_MODULE->module_name,
            myname));
    }

    result = globus_ftp_client_plugin_get_plugin_specific(
        plugin,
        (void **) &ps);

    if(result != GLOBUS_SUCCESS)
    {
        return result;
    }

    globus_mutex_destroy(&ps->lock);
    globus_free(ps);

    return globus_ftp_client_plugin_destroy(plugin);
}

/**
 * Retrieve user specific pointer
 * @ingroup globus_ftp_client_perf_plugin
 *
 * @param plugin
 *        plugin previously initialized with init (above)
 *
 * @param user_specific
 *        pointer to storage for user_specific pointer
 *
 * @return
 *        - GLOBUS_SUCCESS
 *        - Error on NULL plugin
 *        - Error on NULL user_specific
 */

globus_result_t
globus_ftp_client_perf_plugin_get_user_specific(
    globus_ftp_client_plugin_t *                    plugin,
    void **                                         user_specific)
{
    globus_result_t                                 result;
    perf_plugin_info_t *                            ps;
    static char *                                   myname =
        "globus_ftp_client_perf_plugin_get_user_specific";

    if(plugin == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
            GLOBUS_FTP_CLIENT_MODULE,
            GLOBUS_NULL,
            "[%s] NULL plugin at %s\n",
            GLOBUS_FTP_CLIENT_MODULE->module_name,
            myname));
    }

    if(user_specific == GLOBUS_NULL)
    {
        return globus_error_put(globus_error_construct_string(
            GLOBUS_FTP_CLIENT_MODULE,
            GLOBUS_NULL,
            "[%s] NULL user_specific at %s\n",
            GLOBUS_FTP_CLIENT_MODULE->module_name,
            myname));
    }

    result = globus_ftp_client_plugin_get_plugin_specific(
        plugin,
        (void **) &ps);

    if(result != GLOBUS_SUCCESS)
    {
        return result;
    }

    *user_specific = ps->user_specific;

    return GLOBUS_SUCCESS;
}
