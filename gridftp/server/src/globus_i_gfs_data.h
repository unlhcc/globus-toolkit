#ifndef GLOBUS_I_GFS_DATA_H
#define GLOBUS_I_GFS_DATA_H

#include "globus_i_gridftp_server.h"

extern globus_i_gfs_data_attr_t         globus_i_gfs_data_attr_defaults;


typedef globus_gfs_finished_info_t      globus_gfs_data_reply_t;
typedef globus_gfs_event_info_t         globus_gfs_data_event_reply_t;

typedef void
(*globus_i_gfs_data_callback_t)(
    globus_gfs_data_reply_t *           reply,
    void *                              user_arg);

typedef void
(*globus_i_gfs_data_event_callback_t)(
    globus_gfs_data_event_reply_t *     reply,
    void *                              user_arg);

void
globus_i_gfs_data_init();

globus_result_t
globus_i_gfs_data_request_stat(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    int                                 id,
    globus_gfs_stat_info_t *            stat_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

globus_result_t
globus_i_gfs_data_request_recv(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    int                                 id,
    globus_gfs_transfer_info_t *        recv_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg);

globus_result_t
globus_i_gfs_data_request_send(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    int                                 id,
    globus_gfs_transfer_info_t *        send_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg);

globus_result_t
globus_i_gfs_data_request_list(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    int                                 id,
    globus_gfs_transfer_info_t *        list_info,
    globus_i_gfs_data_callback_t        cb,
    globus_i_gfs_data_event_callback_t  event_cb,
    void *                              user_arg);

globus_result_t
globus_i_gfs_data_request_command(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    int                                 id,
    globus_gfs_command_info_t *         command_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

globus_result_t
globus_i_gfs_data_request_passive(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    int                                 id,
    globus_gfs_data_info_t *            data_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

globus_result_t
globus_i_gfs_data_request_active(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    int                                 id,
    globus_gfs_data_info_t *            data_info,
    globus_i_gfs_data_callback_t        cb,
    void *                              user_arg);

void
globus_i_gfs_data_destroy_handle(
    globus_i_gfs_data_handle_t *        data_handle);

void
globus_i_gfs_data_request_transfer_event(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    int                                 transfer_id,
    int                                 event_type);
    
globus_result_t
globus_i_gfs_data_node_start(
    globus_xio_handle_t                 handle,
    globus_xio_system_handle_t          system_handle,
    const char *                        remote_contact);

void
globus_i_gfs_data_session_start(
    void **                             user_handle,
    globus_gfs_ipc_handle_t             ipc_handle,
    const char *                        user_dn);

void
globus_i_gfs_data_session_stop(
    void *                              user_handle,
    globus_gfs_ipc_handle_t             ipc_handle);

#endif
