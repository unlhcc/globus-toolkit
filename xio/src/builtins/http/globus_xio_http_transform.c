#include "globus_i_xio_http.h"
#include "globus_i_xio.h"
#include <limits.h>

/**
 * @defgroup globus_i_xio_http_transform Internal Transform Implementation
 */

static
void
globus_l_xio_http_read_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);

static
globus_result_t
globus_l_xio_http_parse_chunk_header(
    globus_i_xio_http_handle_t *        http_handle,
    globus_bool_t *                     done);

static
void
globus_l_xio_http_read_chunk_header_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg);

/**
 * Open an HTTP URI
 * @ingroup globus_i_xio_http_transform 
 *
 * Opens a new connection to handle an HTTP request. Allocates a handle
 * and then passes the open on to the transport. In the callback called
 * by the transport-level open, the HTTP driver will begin sending or
 * receiving the HTTP request metadata.
 *
 * @param target
 *     HTTP-specific target contact information.
 * @param attr
 *     HTTP attributes to associate with this open. On the client side,
 *     this will allow overriding some information in the target.
 * @param op
 *     XIO Operation associated with this open.
 *
 * @returns This function directly returns GLOBUS_SUCCESS or
 * GLOBUS_XIO_ERROR_MEMORY. Other errors may be generated by
 * globus_i_xio_http_handle_init() or globus_xio_driver_pass_open().
 *
 * @retval GLOBUS_SUCCESS
 *     Open successfully passed to transport.
 * @retval GLOBUS_XIO_ERROR_MEMORY
 *     Unable to allocate an HTTP handle.
 */
globus_result_t
globus_i_xio_http_open(
    void *                              target,
    void *                              attr,
    globus_xio_operation_t              op)
{
    globus_result_t                     result;
    globus_i_xio_http_handle_t *        http_handle;
    globus_i_xio_http_attr_t *          http_attr = attr;
    globus_xio_driver_callback_t        open_callback;
    GlobusXIOName(globus_l_xio_http_open);

    http_handle = globus_libc_calloc(1, sizeof(globus_i_xio_http_handle_t));

    if (http_handle == NULL)
    {
        result = GlobusXIOErrorMemory("http_handle");

        goto error_exit;
    }
    result = globus_i_xio_http_handle_init(
            http_handle,
            http_attr,
            target);

    if (result != GLOBUS_SUCCESS)
    {
        goto free_http_handle_exit;

        return result;
    }

    if (http_handle->target_info.is_client)
    {
        open_callback = globus_i_xio_http_client_open_callback;
    }
    else
    {
        open_callback = globus_i_xio_http_server_open_callback;
    }

    result = globus_xio_driver_pass_open(
        &http_handle->handle,
        op,
        open_callback,
        http_handle);

    if (result != GLOBUS_SUCCESS)
    {
        goto destroy_http_handle;
    }
    return result;

destroy_http_handle:
    globus_i_xio_http_handle_destroy(http_handle);
free_http_handle_exit:
    globus_libc_free(http_handle);
error_exit:
    return result;
}
/* globus_i_xio_http_open() */

/**
 * Read from an HTTP stream
 * @ingroup globus_i_xio_http_transform
 *
 * Begins processing a read. If this is called before the headers have been
 * parsed, then the information pertaining to the read is just stored to
 * be processed later. Otherwise globus_i_xio_http_copy_residue() is called
 * to handle any residual data in the header buffer and pass the read
 * to the transport if necessary.
 *
 * @param handle
 *     Void pointer to a globus_i_xio_http_handle_t.
 * @param iovec
 *     Pointer to user's iovec array. Can't be changed, so a copy is made
 *     in the http_handle.
 * @param iovec_count
 *     Length of the iovec array.
 * @param op
 *     Operation associated with the read.
 *
 * @return
 *     This function returns GLOBUS_SUCCESS, 
 *     GLOBUS_XIO_ERROR_ALREADY_REGISTERED, and GLOBUS_XIO_ERROR_EOF
 *     errors directly.
 *
 * @retval GLOBUS_SUCCESS
 *     Read handled successfully. At some point later,
 *     globus_xio_driver_finished_read() will be called.
 * @retval GLOBUS_XIO_ERROR_ALREADY_REGISTERED
 *     A read is already registered with the HTTP handle, so we can't deal
 *     with this new one.
 * @retval GLOBUS_XIO_ERROR_EOF
 *     A read is being registered on a handle which won't have any more
 *     entity body available (either none was present or the entire content
 *     length or final chunk has been read.
 *
 * @todo Implement "chunked" transfer-encoding.
 */
globus_result_t
globus_i_xio_http_read(
    void *                              handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_i_xio_http_handle_t *        http_handle = handle;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_i_xio_http_header_info_t *   header_info;
    int                                 i;
    GlobusXIOName(globus_i_xio_http_read);

    if (http_handle->target_info.is_client)
    {
        header_info = &http_handle->response_info.headers;
    }
    else
    {
        header_info = &http_handle->request_info.headers;
    }

    if (http_handle->user_read_operation != NULL)
    {
        /* Only one read in progress per handle, sorry */
        result = GlobusXIOErrorAlreadyRegistered();

        goto error_exit;
    }

    /* Copy user's iovec into non-const version in the handle, so that
     * we can deal with premature reads or reads from the header residue.
     */
    http_handle->user_read_iov = globus_libc_calloc(
            iovec_count, sizeof(globus_xio_iovec_t));
    http_handle->user_read_iovcnt = iovec_count;
    http_handle->user_read_operation = op;
    http_handle->user_read_nbytes = 0;
    http_handle->user_read_wait_for = GlobusXIOOperationGetWaitFor(op);

    for (i = 0; i < iovec_count; i++)
    {
        http_handle->user_read_iov[i].iov_base = 
            iovec[i].iov_base;
        http_handle->user_read_iov[i].iov_len =
            iovec[i].iov_len;
    }

    if (http_handle->parsed_headers)
    {
        /* If the headers are parsed already, we can either copy data from
         * the residue into the buffers or pass the read to the transport
         */
        if (! header_info->entity_needed)
        {
            /* No entity associated with response */
            result = GlobusXIOErrorEOF();

            goto free_iovec_error;
        }
        else
        {
            /* Copy already-read information into user's iovec. If the
             * read isn't satisfied here, then it will be passed to the
             * transport.
             */
            globus_i_xio_http_copy_residue(http_handle);
        }
    }
    return result;

free_iovec_error:
    globus_libc_free(http_handle->user_read_iov);
    http_handle->user_read_iov = NULL;
    http_handle->user_read_iovcnt = 0;
    http_handle->user_read_operation = NULL;
    http_handle->user_read_nbytes = 0;

error_exit:
    return result;
}
/* globus_i_xio_http_read() */

/**
 * Copy residual data into user buffers.
 * @ingroup globus_i_xio_http_transform
 *
 * Copies any data in the @a http_handle's read_buffer into a user buffer
 * passed to the http driver via globus_xio_http_driver_pass_read(). If
 * there is sufficient data in the buffer residue to satisfy the "wait_for"
 * value passed to the read function, then this will cause
 * globus_xio_driver_finished_open() to be called. Otherwise, the driver
 * will pass the read to the transport driver.
 *
 * @param http_handle
 *     Handle associated with the read.
 * 
 * @return void
 */
void
globus_i_xio_http_copy_residue(
    globus_i_xio_http_handle_t *        http_handle)
{
    int                                 i;
    int                                 to_copy;
    globus_result_t                     result = GLOBUS_SUCCESS;
    globus_i_xio_http_header_info_t *   headers;
    globus_size_t                       nbytes;
    globus_bool_t                       done;
    globus_size_t                       max_content = 0;
    globus_xio_operation_t              op;
    GlobusXIOName(globus_i_xio_http_copy_residue);

    if (http_handle->target_info.is_client)
    {
        headers = &http_handle->response_info.headers;
    }
    else
    {
        headers = &http_handle->request_info.headers;
    }

    if (http_handle->user_read_iovcnt == 0)
    {
        /* Read must have been cancelled? */
        return;
    }
    if (! headers->entity_needed)
    {
        /* No entity */
        result = GlobusXIOErrorEOF();

        goto finish;
    }

    while (headers->entity_needed)
    {
        /* Deal with read if an entity is expected. If it isn't, then
         * any data in the read buffer is likely a pipelined request or
         * response.
         */
        if ((headers->transfer_encoding
                == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED) &&
            (http_handle->read_chunk_left == 0))
        {
            /* Get another chunk header out of the residue */
            result = globus_l_xio_http_parse_chunk_header(
                    http_handle,
                    &done);

            if (result == GLOBUS_SUCCESS && !done)
            {
                /*
                 * Didn't get the complete chunk header, we'll need to
                 * register another read.
                 */
                result = globus_i_xio_http_clean_read_buffer(http_handle);

                if (result != GLOBUS_SUCCESS)
                {
                    goto finish;
                }

                result = globus_xio_driver_pass_read(
                        http_handle->user_read_operation,
                        &http_handle->read_iovec,
                        1,
                        1,
                        globus_l_xio_http_read_chunk_header_callback,
                        http_handle);
                goto finish;
            }
            else if (result != GLOBUS_SUCCESS)
            {
                /* Error parsing chunk size */
                goto finish;
            }
            else
            {
                /* Read a new chunk header---let's restart this loop */
                continue;
            }
        }
        else if (headers->content_length_set)
        {
            /* Don't wait beyond content-length */
            if (http_handle->user_read_wait_for > headers->content_length)
            {
                http_handle->user_read_wait_for = headers->content_length;
            }
        }
        to_copy = 0;
        /*
         * Copy already read data into the user's buffers.
         */
        for (i = 0;
             (i < http_handle->user_read_iovcnt)
                 && (http_handle->read_buffer_valid > 0);
             i++)
        {
            if (http_handle->user_read_iov[i].iov_len
                    > http_handle->read_buffer_valid)
            {
                to_copy = http_handle->read_buffer_valid;
            }
            else
            {
                to_copy = http_handle->user_read_iov[i].iov_len;
            }
            /* Don't copy more than content-length */
            if (headers->transfer_encoding !=
                    GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED &&
                headers->content_length_set &&
                headers->content_length < to_copy)
            {
                to_copy = headers->content_length;
            }
            else if (headers->transfer_encoding
                    == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED &&
                    http_handle->read_chunk_left < to_copy)
            {
                to_copy = http_handle->read_chunk_left;
            }

            if (to_copy == 0)
            {
                continue;
            }
            memcpy(http_handle->user_read_iov[i].iov_base,
                    (globus_byte_t *)http_handle->read_buffer.iov_base +
                    http_handle->read_buffer_offset,
                    to_copy);
            http_handle->read_buffer_valid -= to_copy;
            http_handle->read_buffer_offset += to_copy;
            http_handle->user_read_iov[i].iov_len -= to_copy;
            http_handle->user_read_iov[i].iov_base += to_copy;
            http_handle->user_read_nbytes += to_copy;
            if (http_handle->user_read_wait_for >= to_copy)
            {
                http_handle->user_read_wait_for -= to_copy;
            }
            else
            {
                http_handle->user_read_wait_for = 0;
            }
            http_handle->read_chunk_left -= to_copy;

            if (headers->transfer_encoding
                    != GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED &&
                    headers->content_length_set)
            {
                headers->content_length -= to_copy;
            }
        }
        if (to_copy == 0)
        {
            break;
        }
    }

    if (headers->entity_needed && http_handle->user_read_wait_for > 0)
    {
        /* Haven't pre-read enough, pass to transport */

        if (headers->transfer_encoding
                != GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED
            && headers->content_length_set)
        {
            max_content = headers->content_length;
        }
        else if (headers->transfer_encoding
                == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED)
        {
            max_content = http_handle->read_chunk_left;
        }

        if (max_content > 0)
        {
            /*
             * Need to truncate iovecs here, so that we don't try to read
             * past end of content. XIO will repost the read to us if we
             * finish with < wait_for bytes.
             */
            nbytes = 0;

            for (i = 0; i < http_handle->user_read_iovcnt; i++)
            {
                if ((http_handle->user_read_iov[i].iov_len + nbytes)
                        > max_content)
                {
                    http_handle->user_read_iov[i].iov_len =
                        headers->content_length - nbytes;
                    nbytes += http_handle->user_read_iov[i].iov_len;
                }
            }
        }
        result = globus_xio_driver_pass_read(
                http_handle->user_read_operation,
                http_handle->user_read_iov,
                http_handle->user_read_iovcnt,
                http_handle->user_read_wait_for,
                globus_l_xio_http_read_callback,
                http_handle);
    }
finish:
    if (http_handle->user_read_wait_for <= 0 || result != GLOBUS_SUCCESS)
    {
        if (headers->transfer_encoding !=
                GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED &&
            headers->content_length_set &&
            headers->content_length == 0)
        {
            /* Synthesize EOF if we've read all of the entity content */
            result = GlobusXIOErrorEOF();
        }
        /*
         * Either we've read enough, no entity was present, or pass to
         * transport failed. Call finished_read 
         */
        op = http_handle->user_read_operation;

        nbytes = http_handle->user_read_nbytes;
        globus_libc_free(http_handle->user_read_iov);
        http_handle->user_read_iov = NULL;
        http_handle->user_read_iovcnt = 0;
        http_handle->user_read_operation = NULL;
        http_handle->user_read_nbytes = 0;

        globus_xio_driver_finished_read(op, result, nbytes);
    }
    return;
}
/* globus_i_xio_http_copy_residue() */

static
void
globus_l_xio_http_read_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_i_xio_http_handle_t *        http_handle = user_arg;
    globus_i_xio_http_header_info_t *   headers;
    GlobusXIOName(globus_l_xio_http_read_callback);

    if (http_handle->target_info.is_client)
    {
        headers = &http_handle->response_info.headers;
    }
    else
    {
        headers = &http_handle->request_info.headers;
    }

    globus_libc_free(http_handle->user_read_iov);
    http_handle->user_read_iov = NULL;
    http_handle->user_read_iovcnt = 0;
    http_handle->user_read_operation = NULL;
    http_handle->user_read_nbytes = 0;

    if (headers->transfer_encoding
            != GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED &&
        headers->content_length_set)
    {
        headers->content_length -= nbytes;

        if (headers->content_length == 0 && result == GLOBUS_SUCCESS)
        {
            result = GlobusXIOErrorEOF();
        }
    }
    else if (headers->transfer_encoding
            == GLOBUS_XIO_HTTP_TRANSFER_ENCODING_CHUNKED)
    {
        http_handle->read_chunk_left -= nbytes;
    }

    globus_xio_driver_finished_read(op, result, nbytes);
}
/* globus_l_xio_http_read_callback() */

static
void
globus_l_xio_http_read_chunk_header_callback(
    globus_xio_operation_t              op,
    globus_result_t                     result,
    globus_size_t                       nbytes,
    void *                              user_arg)
{
    globus_i_xio_http_handle_t *        http_handle = user_arg;

    http_handle->read_buffer_valid += nbytes;

    globus_i_xio_http_copy_residue(http_handle);
}
/* globus_l_xio_http_read_chunk_header_callback() */

static
globus_result_t
globus_l_xio_http_parse_chunk_header(
    globus_i_xio_http_handle_t *        http_handle,
    globus_bool_t *                     done)
{
    char *                              current_offset;
    char *                              eol;
    unsigned long                       chunk_size;
    globus_result_t                     result;
    size_t                              parsed;
    GlobusXIOName(globus_l_xio_http_parse_chunk_header);

    current_offset = ((char *) (http_handle->read_buffer.iov_base))
        + http_handle->read_buffer_offset;

    eol = globus_i_xio_http_find_eol(current_offset,
            http_handle->read_buffer_valid);

    if (eol == NULL)
    {
        *done = GLOBUS_FALSE;
        return GLOBUS_SUCCESS;
    }

    if (!http_handle->first_chunk)
    {
        if (current_offset != eol)
        {
            result = GlobusXIOHttpErrorParse("chunk", current_offset);
        }

        current_offset += 2;
        eol = globus_i_xio_http_find_eol(current_offset,
                http_handle->read_buffer_valid - 2);

        if (eol == NULL)
        {
            *done = GLOBUS_FALSE;
            return GLOBUS_SUCCESS;
        }
    }
    else
    {
        http_handle->first_chunk = GLOBUS_FALSE;
    }
    *eol = '\0';

    globus_libc_lock();
    errno = 0;
    chunk_size = strtoul(current_offset, NULL, 16);
    if (chunk_size == ULONG_MAX && errno != 0)
    {
        result = GlobusXIOHttpErrorParse("Chunk-size", current_offset);

        globus_libc_unlock();

        *done = GLOBUS_FALSE;
        return result;
    }
    globus_libc_unlock();

    if (chunk_size > UINT_MAX)
    {
        result = GlobusXIOHttpErrorParse("Chunk-size", current_offset);

        globus_libc_unlock();
    }
    http_handle->read_chunk_left = (globus_size_t) chunk_size;

    current_offset = eol + 2;
    parsed = current_offset - ((char *) http_handle->read_buffer.iov_base
            + http_handle->read_buffer_offset);

    http_handle->read_buffer_valid -= parsed;
    http_handle->read_buffer_offset += parsed;

    if (http_handle->read_chunk_left != 0)
    {
        *done = GLOBUS_TRUE;
        return GLOBUS_SUCCESS;
    }

    /* We parsed the last chunk size line '0'. Now parse any footers and the
     * final CRLF
     */
    while ((eol = globus_i_xio_http_find_eol(
                    current_offset,
                    http_handle->read_buffer_valid)) != current_offset)
    {
        if (eol == NULL)
        {
            /* final headers not all found */
            *done = GLOBUS_FALSE;
            return GLOBUS_SUCCESS;
        }
        /* Ignore footers for now */
        current_offset = eol + 2;
        parsed = current_offset - ((char *) http_handle->read_buffer.iov_base
                + http_handle->read_buffer_offset);
        http_handle->read_buffer_valid -= parsed;
        http_handle->read_buffer_offset += parsed;
    }

    if (eol != NULL)
    {
        /* We found an empty line---end of footerrs found. */
        *done = GLOBUS_TRUE;
        current_offset = eol + 2;

        parsed = current_offset - ((char *) http_handle->read_buffer.iov_base
                + http_handle->read_buffer_offset);
        http_handle->read_buffer_valid -= parsed;
        http_handle->read_buffer_offset += parsed;

        if (http_handle->target_info.is_client)
        {
            http_handle->response_info.headers.entity_needed = GLOBUS_FALSE;
        }
        else
        {
            http_handle->request_info.headers.entity_needed = GLOBUS_FALSE;
        }
    }
    else
    {
        /* Didn't finish parsing */
        *done = GLOBUS_FALSE;
    }
    return GLOBUS_SUCCESS;
}
/* globus_l_xio_http_parse_chunk_header() */

globus_result_t
globus_i_xio_http_write(
    void *                                  handle,
    const globus_xio_iovec_t *              iovec,
    int                                     iovec_count,
    globus_xio_operation_t                  op)
{
    globus_i_xio_http_handle_t *            http_handle = handle;

    return globus_error_put(GLOBUS_ERROR_NO_INFO);
}
/* globus_i_xio_http_write() */

globus_result_t
globus_i_xio_http_close(
    void *                              handle,
    void *                              attr,
    globus_xio_driver_handle_t          driver_handle,
    globus_xio_operation_t              op)
{
    return globus_xio_driver_pass_close(
            op,
            globus_i_xio_http_close_callback,
            handle);
}
/* globus_i_xio_http_close() */

void
globus_i_xio_http_close_callback(
    globus_xio_operation_t                  op,
    globus_result_t                         result,
    void *                                  user_arg)
{
    globus_bool_t                           call_close_finished;
    globus_i_xio_http_handle_t *            http_handle = user_arg;
    globus_xio_driver_handle_t              driver_handle = http_handle->handle;

    call_close_finished = (http_handle->close_operation == NULL);

    globus_i_xio_http_handle_destroy(user_arg);

    globus_xio_driver_finished_close(op, result);
    globus_xio_driver_handle_close(driver_handle);
}
/* globus_i_xio_http_close_callback() */
