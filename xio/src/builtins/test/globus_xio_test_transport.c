#include "globus_xio_driver.h"
#include "globus_xio_load.h"
#include "globus_i_xio.h"
#include "globus_common.h"
#include "globus_xio_test_transport.h"

#define GLOBUS_XIO_TEST_TRANSPORT_DRIVER_MODULE &globus_i_xio_test_module

#define MAX_DELAY 100000

#define XIOTestCreateOpWraper(ow, _in_dh, _in_op, res, nb)              \
{                                                                       \
    ow = (globus_l_xio_test_op_wrapper_t *)                             \
            globus_malloc(sizeof(globus_l_xio_test_op_wrapper_t));      \
    ow->dh = _in_dh;                                                    \
    ow->op = (_in_op);                                                  \
    ow->res = res;                                                      \
    ow->canceled = GLOBUS_FALSE;                                        \
    ow->nbytes = nb;                                                    \
    globus_mutex_init(&ow->mutex, NULL);                                \
    ow->callback_out = GLOBUS_FALSE;                                    \
}

static int
globus_l_xio_test_activate();

static int
globus_l_xio_test_deactivate();

static
void
globus_l_xio_operation_kickout(
    void *                              user_arg);

/* 
 *  handle and attr are the same structure here
 */
typedef struct globus_l_xio_test_handle_s
{
    globus_xio_test_failure_t           failure;
    globus_bool_t                       inline_finish;
    globus_size_t                       read_nbytes;
    globus_size_t                       chunk_size;
    globus_size_t                       bytes_read;
    globus_reltime_t                    delay;

    int                                 random;
} globus_l_xio_test_handle_t;

typedef struct globus_l_xio_test_op_wrapper_s
{
    globus_xio_operation_type_t         type;
    globus_xio_operation_t              op;
    globus_l_xio_test_handle_t *        dh;
    globus_result_t                     res;
    globus_size_t                       nbytes;
    globus_callback_handle_t            callback_handle;
    globus_bool_t                       callback_out;
    globus_bool_t                       canceled;
    globus_mutex_t                      mutex;
} globus_l_xio_test_op_wrapper_t;


static globus_l_xio_test_handle_t       globus_l_default_attr;

#include "version.h"

globus_module_descriptor_t       globus_i_xio_test_module =
{
    "globus_xio_test",
    globus_l_xio_test_activate,
    globus_l_xio_test_deactivate,
    GLOBUS_NULL,
    GLOBUS_NULL,
    &local_version
};


static void
cancel_cb(
    globus_xio_operation_t                      op,
    void *                                      user_arg)
{
    globus_l_xio_test_op_wrapper_t *    ow;
    globus_bool_t                       active;
    globus_result_t                     res;
    GlobusXIOName(cancel_cb);

    GlobusXIODebugInternalEnter();

    GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE, 
        ("[%s:%d] test driver cancel callback\n", _xio_name, __LINE__));
    ow = (globus_l_xio_test_op_wrapper_t *) user_arg;

    globus_mutex_lock(&ow->mutex);
    {
        ow->res = GlobusXIOErrorTimedout();
        ow->canceled = GLOBUS_TRUE;
        if(ow->callback_out)
        {
            res = globus_callback_unregister(
                ow->callback_handle,
                NULL,
                NULL,
                &active);
            globus_assert(res == GLOBUS_SUCCESS);
            if(!active)
            {
                /* if we could stop it, register it for right now */
                res = globus_callback_space_register_oneshot(
                    NULL,
                    NULL,
                    globus_l_xio_operation_kickout,
                    (void *) ow,
                    GLOBUS_CALLBACK_GLOBAL_SPACE);
                globus_assert(res == GLOBUS_SUCCESS);
            }
            ow->callback_out = GLOBUS_FALSE;
        }
    }
    globus_mutex_unlock(&ow->mutex);
    GlobusXIODebugInternalExit();
}

static void
test_l_delay_register(
    globus_l_xio_test_op_wrapper_t *        ow,
    globus_reltime_t *                      delay)
{
    globus_result_t                         res;
    GlobusXIOName(test_l_delay_register);

    GlobusXIODebugInternalEnter();
    globus_mutex_lock(&ow->mutex);
    {
        if(ow->canceled)
        {
            delay = NULL;
            ow->res = GlobusXIOErrorTimedout();
        }
        ow->callback_out = GLOBUS_TRUE;
        res = globus_callback_space_register_oneshot(
            &ow->callback_handle,
            delay,
            globus_l_xio_operation_kickout,
            (void *) ow,
            GLOBUS_CALLBACK_GLOBAL_SPACE);
                                                                                
        globus_assert(res == GLOBUS_SUCCESS);
    }
    globus_mutex_unlock(&ow->mutex);
    GlobusXIODebugInternalExit();
}

static void
test_inline_blocker(
    globus_reltime_t *                          delay)
{
    globus_abstime_t                            timeout;
    globus_reltime_t                            zero;
    int                                         sec;
    int                                         usec;
    GlobusXIOName(test_inline_blocker);

    GlobusXIODebugInternalEnter();
    GlobusTimeReltimeSet(zero, 0, 0);
    if(globus_reltime_cmp(delay, &zero) != 0)
    {
        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE, 
            ("nonzero delay\n"));
        GlobusTimeAbstimeGetCurrent(timeout);
        GlobusTimeAbstimeInc(timeout, *delay);
        GlobusTimeReltimeGet(*delay, sec, usec);
        sleep(sec);
        globus_libc_usleep(usec);
    }
    GlobusXIODebugInternalExit();
}

/*
 *  initialize a driver attribute
 */
static globus_result_t
globus_l_xio_test_attr_init(
    void **                             out_attr)
{
    globus_l_xio_test_handle_t *        attr;
    GlobusXIOName(globus_l_xio_test_attr_init);

    GlobusXIODebugInternalEnter();
    attr = (globus_l_xio_test_handle_t *)
                globus_malloc(sizeof(globus_l_xio_test_handle_t));
    memset(attr, '\0', sizeof(globus_l_xio_test_handle_t));
    attr->inline_finish = GLOBUS_FALSE;
    attr->failure = 0;  /* default is no failures */
    GlobusTimeReltimeSet(attr->delay, 0, 0);
    attr->read_nbytes = -1; /* default is no EOF (close only) */
    attr->chunk_size = -1; /* default: entire chunk requested */

    *out_attr = attr;
    GlobusXIODebugInternalExit();

    return GLOBUS_SUCCESS;
}

static void
test_get_delay_time(
    globus_l_xio_test_handle_t *        dh,
    globus_reltime_t *                  out_delay)
{
    int                                 usec = 0;
    GlobusXIOName(test_get_delay_time);

    GlobusXIODebugInternalEnter();
    if(dh->random)
    {
        usec = rand() % MAX_DELAY;
        GlobusTimeReltimeSet(*out_delay, 0, usec);
    }
    GlobusXIODebugInternalExit();
}

/*
 *  modify the attribute structure
 */
static globus_result_t
globus_l_xio_test_attr_cntl(
    void *                              driver_attr,
    int                                 cmd,
    va_list                             ap)
{
    globus_l_xio_test_handle_t *        attr;
    int                                 usecs;
    GlobusXIOName(globus_l_xio_test_attr_cntl);

    GlobusXIODebugInternalEnter();

    attr = (globus_l_xio_test_handle_t *) driver_attr;

    switch(cmd)
    {
        case GLOBUS_XIO_TEST_SET_INLINE:
            attr->inline_finish = va_arg(ap, int);
            break;

        case GLOBUS_XIO_TEST_SET_FAILURES:
            attr->failure = va_arg(ap, int);
            break;

        case GLOBUS_XIO_TEST_SET_USECS:
            usecs = va_arg(ap, int);
            GlobusTimeReltimeSet(attr->delay, 0, usecs);
            break;

        case GLOBUS_XIO_TEST_READ_EOF_BYTES:
            attr->read_nbytes = va_arg(ap, int);
            break;

        case GLOBUS_XIO_TEST_CHUNK_SIZE:
            attr->chunk_size = va_arg(ap, int);
            break;

        case GLOBUS_XIO_TEST_RANDOM:
            attr->random = GLOBUS_TRUE;
            usecs = va_arg(ap, int);
            srand(usecs);
        GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE, 
            ("turning on random, seed=%d\n", usecs));
            break;

    }
    GlobusXIODebugInternalExit();

    return GLOBUS_SUCCESS;
}

/*
 *  copy an attribute structure
 */
static
globus_result_t
globus_l_xio_test_attr_copy(
    void **                             dst,
    void *                              src)
{
    globus_l_xio_test_handle_t *        attr;
    GlobusXIOName(globus_l_xio_test_attr_copy);

    GlobusXIODebugInternalEnter();

    attr = (globus_l_xio_test_handle_t *)
                globus_malloc(sizeof(globus_l_xio_test_handle_t));
    memcpy(attr, src, sizeof(globus_l_xio_test_handle_t));

    *dst = attr;
    GlobusXIODebugInternalExit();

    return GLOBUS_SUCCESS;
}

/*
 *  destroy an attr structure
 */
static
globus_result_t
globus_l_xio_test_attr_destroy(
    void *                              driver_attr)
{
    globus_free(driver_attr);

    return GLOBUS_SUCCESS;
}

/*
 *  initialize target structure
 */
static
globus_result_t
globus_l_xio_test_target_init(
    void **                                 out_driver_target,
    globus_xio_operation_t                  target_op,
    const globus_xio_contact_t *            contact_info,
    void *                                  driver_attr)
{
    return GLOBUS_SUCCESS;
}

/*
 *  destroy the target structure
 */
static globus_result_t
globus_l_xio_test_target_destroy(
    void *                              driver_target)
{
    return GLOBUS_SUCCESS;
}

static void 
globus_l_xio_operation_kickout(
    void *                              user_arg)
{
    globus_l_xio_test_op_wrapper_t *    ow;
    GlobusXIOName(globus_l_xio_operation_kickout);

    GlobusXIODebugInternalEnter();

    ow = (globus_l_xio_test_op_wrapper_t *) user_arg;

    globus_assert(ow != GLOBUS_SUCCESS);

    GlobusXIODriverDisableCancel(ow->op);

    GlobusXIODebugPrintf(GLOBUS_XIO_DEBUG_INFO_VERBOSE, 
        ("[globus_l_xio_test_operation_kickout] : finishing with=%d\n", 
        ow->res));

    globus_mutex_lock(&ow->mutex);
    {
        switch(ow->type)
        {
            case GLOBUS_XIO_OPERATION_TYPE_OPEN:
                globus_xio_driver_finished_open(
                    NULL, ow->dh, ow->op, ow->res);
                if(ow->res != GLOBUS_SUCCESS)
                {
                    globus_l_xio_test_attr_destroy(ow->dh);
                }
                break;

            case GLOBUS_XIO_OPERATION_TYPE_CLOSE:
                globus_xio_driver_finished_close(ow->op, ow->res);
                globus_l_xio_test_attr_destroy(ow->dh);
                break;

            case GLOBUS_XIO_OPERATION_TYPE_READ:
                globus_xio_driver_finished_read(ow->op, ow->res, ow->nbytes);
                break;

            case GLOBUS_XIO_OPERATION_TYPE_WRITE:
                globus_xio_driver_finished_write(ow->op, ow->res, ow->nbytes);
                break;

            case GLOBUS_XIO_OPERATION_TYPE_ACCEPT:
                globus_xio_driver_finished_accept(ow->op, NULL, ow->res);
                break;

            default:
                globus_assert(0);
        }
    }
    globus_mutex_unlock(&ow->mutex);

    globus_mutex_destroy(&ow->mutex);
    globus_free(ow);

    GlobusXIODebugInternalExit();
}

/**********************************
 *  server stuff
 *********************************/

static globus_result_t
globus_l_xio_test_server_init(
    void **                             out_server,
    void *                              driver_attr)
{
    globus_l_xio_test_handle_t *        server;

    if(driver_attr == NULL)
    {
        driver_attr = &globus_l_default_attr;
    }

    /* copy the attr to a handle */
    globus_l_xio_test_attr_copy((void **)&server, driver_attr);

    *out_server = server;
    
    return GLOBUS_SUCCESS;
}

static globus_result_t
globus_l_xio_test_accept(
    void *                              driver_server,
    void *                              driver_attr,
    globus_xio_operation_t              accept_op)
{
    globus_reltime_t                    end_time;
    globus_l_xio_test_handle_t *        server;
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_l_xio_test_accept);

    GlobusXIODebugInternalEnter();

    server = (globus_l_xio_test_handle_t *) driver_server;

    if(server->failure == GLOBUS_XIO_TEST_FAIL_PASS_ACCEPT)
    {
        return GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_PASS_ACCEPT);
    }
    else if(server->failure == GLOBUS_XIO_TEST_FAIL_FINISH_ACCEPT)
    {
        res = GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_FINISH_ACCEPT);
    }

    if(server->inline_finish)
    {
        globus_xio_driver_finished_accept(accept_op, NULL, res);
    }
    else
    {
        globus_l_xio_test_op_wrapper_t *    ow;
        globus_reltime_t *                  delay;
        globus_bool_t                       canceled;

        XIOTestCreateOpWraper(ow, server, accept_op, res, 0);
        ow->type = GLOBUS_XIO_OPERATION_TYPE_ACCEPT;

        delay = &server->delay;
        GlobusXIODriverEnableCancel(accept_op, canceled, cancel_cb, ow);

        if(canceled)
        {
            delay = NULL;
            ow->canceled = GLOBUS_TRUE;
        }
        else
        {
            if(server->random)
            {
                test_get_delay_time(server, &end_time);
                delay = &end_time;
            }

            test_l_delay_register(ow, delay);
        }
    }

    GlobusXIODebugInternalExit();

    return GLOBUS_SUCCESS;
}

static globus_result_t
globus_l_xio_test_server_cntl(
    void *                              driver_server,
    int                                 cmd,
    va_list                             ap)
{
    return GLOBUS_SUCCESS;
}

static globus_result_t
globus_l_xio_test_server_destroy(
    void *                              driver_server)
{
    globus_free(driver_server);
    return GLOBUS_SUCCESS;
}



/*
 *  open a file
 */
static
globus_result_t
globus_l_xio_test_open(
    void *                              driver_target,
    void *                              driver_attr,
    globus_xio_operation_t              op)
{
    globus_l_xio_test_handle_t *        attr;
    globus_l_xio_test_handle_t *        dh;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_reltime_t *                  delay;
    globus_reltime_t                    end_time;
    GlobusXIOName(globus_l_xio_test_open);

    GlobusXIODebugInternalEnter();
    attr = (globus_l_xio_test_handle_t *) driver_attr;

    if(attr == NULL)
    {
        attr = &globus_l_default_attr;
    }

    /* copy the attr to a handle */
    globus_l_xio_test_attr_copy((void **)&dh, attr);
    if(dh->failure == GLOBUS_XIO_TEST_FAIL_PASS_OPEN)
    {
        globus_l_xio_test_attr_destroy(dh);
        return GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_PASS_OPEN);
    }
    else if(dh->failure == GLOBUS_XIO_TEST_FAIL_FINISH_OPEN)
    {
        res = GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_FINISH_OPEN);
    }

    if(dh->inline_finish)
    {
        test_inline_blocker(&dh->delay);
        globus_xio_driver_finished_open(NULL, dh, op, res);
        if(res != GLOBUS_SUCCESS)
        {
            globus_l_xio_test_attr_destroy(dh);
        }
    }
    else
    {
        globus_l_xio_test_op_wrapper_t *    ow;
        globus_bool_t                       canceled;

        XIOTestCreateOpWraper(ow, dh, op, res, 0);
        ow->type = GLOBUS_XIO_OPERATION_TYPE_OPEN;

        delay = &dh->delay;
        GlobusXIODriverEnableCancel(op, canceled, cancel_cb, ow);
        if(canceled)
        {
            delay = NULL;
            ow->canceled = GLOBUS_TRUE;
        }
        else 
        {
            if(dh->random)
            {
                test_get_delay_time(dh, &end_time);
                delay = &end_time;
            }
            test_l_delay_register(ow, delay);
        }
    }
    GlobusXIODebugInternalExit();

    return GLOBUS_SUCCESS;
}

/*
 *  close a file
 */
static
globus_result_t
globus_l_xio_test_close(
    void *                              driver_specific_handle,
    void *                              attr,
    globus_xio_driver_handle_t          driver_handle,
    globus_xio_operation_t              op)
{
    globus_l_xio_test_handle_t *        dh;
    globus_result_t                     res = GLOBUS_SUCCESS;
    GlobusXIOName(globus_l_xio_test_close);

    GlobusXIODebugInternalEnter();

    dh = (globus_l_xio_test_handle_t *) driver_specific_handle;

    if(dh->failure == GLOBUS_XIO_TEST_FAIL_PASS_CLOSE)
    {
        res = GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_PASS_CLOSE);
        goto err;
    }
    else if(dh->failure == GLOBUS_XIO_TEST_FAIL_FINISH_CLOSE)
    {
        res = GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_FINISH_CLOSE);
    }

    if(dh->inline_finish)
    {
        test_inline_blocker(&dh->delay);
        globus_xio_driver_finished_close(op, res);
        globus_l_xio_test_attr_destroy(dh);
    }
    else
    {
        globus_l_xio_test_op_wrapper_t *    ow;
        globus_reltime_t *                  delay;
        globus_bool_t                       canceled;
        globus_reltime_t                    end_time;

        XIOTestCreateOpWraper(ow, dh, op, res, 0);
        ow->type = GLOBUS_XIO_OPERATION_TYPE_CLOSE;

        delay = &dh->delay;
        GlobusXIODriverEnableCancel(op, canceled, cancel_cb, ow);
        if(canceled)
        {
            delay = NULL;
            ow->canceled = GLOBUS_TRUE;
        }
        else 
        {
            if(dh->random)
            {
                test_get_delay_time(dh, &end_time);
                delay = &end_time;
            }
        }
        test_l_delay_register(ow, delay);
    }

    GlobusXIODebugInternalExit();
    return GLOBUS_SUCCESS;

  err:

    GlobusXIODebugInternalExitWithError();
    return res;
}

/*
 *  read from a file
 */
static
globus_result_t
globus_l_xio_test_read(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_test_handle_t *        dh;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_size_t                       nbytes;
    int                                 ctr;
    globus_size_t                       tb;
    GlobusXIOName(globus_l_xio_test_read);

    GlobusXIODebugInternalEnter();

    dh = (globus_l_xio_test_handle_t *) driver_specific_handle;

    if(dh->failure == GLOBUS_XIO_TEST_FAIL_PASS_READ)
    {
        return GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_PASS_READ);
    }
    else if(dh->failure == GLOBUS_XIO_TEST_FAIL_FINISH_READ)
    {
        res = GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_FINISH_READ);
    }

    nbytes = dh->chunk_size;
    if(dh->chunk_size == -1)
    {
        nbytes = GlobusXIOOperationGetWaitFor(op);
    }

    tb = 0;
    for(ctr = 0; ctr < iovec_count; ctr++)
    {
        tb += iovec[ctr].iov_len;
    }
    if(nbytes > tb)
    {
        nbytes = tb;
    }

    dh->bytes_read += nbytes;
    if(dh->read_nbytes != -1 && dh->bytes_read >= dh->read_nbytes)
    {
        res = GlobusXIOErrorEOF();
    }

    if(dh->inline_finish)
    {
        test_inline_blocker(&dh->delay);
        globus_xio_driver_finished_read(op, res, nbytes);
    }
    else
    {
        globus_l_xio_test_op_wrapper_t *    ow;
        globus_reltime_t *                  delay;
        globus_bool_t                       canceled;
        globus_reltime_t                    end_time;

        XIOTestCreateOpWraper(ow, dh, op, res, nbytes);
        ow->type = GLOBUS_XIO_OPERATION_TYPE_READ;

        delay = &dh->delay;
        GlobusXIODriverEnableCancel(op, canceled, cancel_cb, ow);
        if(canceled)
        {
            delay = NULL;
            ow->canceled = GLOBUS_TRUE;
        }
        else 
        {
            if(dh->random)
            {
                test_get_delay_time(dh, &end_time);
                delay = &end_time;
            }
        }
        test_l_delay_register(ow, delay);
    }
    GlobusXIODebugInternalExit();

    return GLOBUS_SUCCESS;
}

/*
 *  write to a file
 */
static
globus_result_t
globus_l_xio_test_write(
    void *                              driver_specific_handle,
    const globus_xio_iovec_t *          iovec,
    int                                 iovec_count,
    globus_xio_operation_t              op)
{
    globus_l_xio_test_handle_t *        dh;
    globus_result_t                     res = GLOBUS_SUCCESS;
    globus_size_t                       nbytes;
    int                                 ctr;
    globus_size_t                       tb;
    GlobusXIOName(globus_l_xio_test_write);

    GlobusXIODebugInternalEnter();
    
    dh = (globus_l_xio_test_handle_t *) driver_specific_handle;
    
    if(dh->failure == GLOBUS_XIO_TEST_FAIL_PASS_WRITE)
    {
        GlobusXIODebugInternalExitWithError();
        return GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_PASS_WRITE);
    }
    else if(dh->failure == GLOBUS_XIO_TEST_FAIL_FINISH_WRITE)
    {
        res = GlobusXIOErrorTestError(GLOBUS_XIO_TEST_FAIL_FINISH_WRITE);
    }

    nbytes = dh->chunk_size;
    if(dh->chunk_size == -1)
    {
        nbytes = GlobusXIOOperationGetWaitFor(op);
    }

    tb = 0;
    for(ctr = 0; ctr < iovec_count; ctr++)
    {
        tb += iovec[ctr].iov_len;
    }
    if(nbytes > tb)
    {
        nbytes = tb;
    }


    if(dh->inline_finish)
    {
        test_inline_blocker(&dh->delay);
        globus_xio_driver_finished_write(op, res, nbytes);
    }
    else
    {
        globus_l_xio_test_op_wrapper_t *    ow;
        globus_reltime_t *                  delay;
        globus_bool_t                       canceled;
        globus_reltime_t                    end_time;

        XIOTestCreateOpWraper(ow, dh, op, res, nbytes);
        ow->type = GLOBUS_XIO_OPERATION_TYPE_WRITE;

        delay = &dh->delay;
        GlobusXIODriverEnableCancel(op, canceled, cancel_cb, ow);
        if(canceled)
        {
            delay = NULL;
            ow->canceled = GLOBUS_TRUE;
        }
        else 
        {
            if(dh->random)
            {
                test_get_delay_time(dh, &end_time);
                delay = &end_time;
            }
        }
        test_l_delay_register(ow, delay);
    }

    GlobusXIODebugInternalExit();
    return GLOBUS_SUCCESS;
}

static
globus_result_t
globus_l_xio_test_cntl(
    void *                              driver_specific_handle,
    int                                 cmd,
    va_list                             ap)
{
    return GLOBUS_SUCCESS;
}

static globus_result_t
globus_l_xio_test_transport_load(
    globus_xio_driver_t *               out_driver,
    va_list                             ap)
{
    globus_xio_driver_t                 driver;
    globus_result_t                     res;

    res = globus_xio_driver_init(&driver, "test", NULL);
    if(res != GLOBUS_SUCCESS)
    {
        return res;
    }

    globus_xio_driver_set_transport(
        driver,
        globus_l_xio_test_open,
        globus_l_xio_test_close,
        globus_l_xio_test_read,
        globus_l_xio_test_write,
        globus_l_xio_test_cntl);

    globus_xio_driver_set_client(
        driver,
        globus_l_xio_test_target_init,
        NULL,
        globus_l_xio_test_target_destroy);

    globus_xio_driver_set_server(
        driver,
        globus_l_xio_test_server_init,
        globus_l_xio_test_accept,
        globus_l_xio_test_server_destroy,
        globus_l_xio_test_server_cntl,
        globus_l_xio_test_target_destroy);

    globus_xio_driver_set_attr(
        driver,
        globus_l_xio_test_attr_init,
        globus_l_xio_test_attr_copy,
        globus_l_xio_test_attr_cntl,
        globus_l_xio_test_attr_destroy);

    *out_driver = driver;

    return GLOBUS_SUCCESS;
}

static void
globus_l_xio_test_transport_unload(
    globus_xio_driver_t                 driver)
{
    globus_xio_driver_destroy(driver);
}



static
int
globus_l_xio_test_activate(void)
{
    int                                 rc;
    globus_l_xio_test_handle_t *        attr;

    rc = globus_module_activate(GLOBUS_COMMON_MODULE);
    if(rc != GLOBUS_SUCCESS)
    {
        return rc;
    }

    attr = &globus_l_default_attr;

    memset(attr, '\0', sizeof(globus_l_xio_test_handle_t));
    attr->inline_finish = GLOBUS_FALSE;
    attr->failure = 0;  /* default is no failures */
    GlobusTimeReltimeSet(attr->delay, 0, 0);
    attr->read_nbytes = -1; /* default is no EOF (close only) */
    attr->chunk_size = -1; /* default: entire chunk requested */

    return GLOBUS_SUCCESS;
}

static
int
globus_l_xio_test_deactivate(void)
{
    return globus_module_deactivate(GLOBUS_COMMON_MODULE);
}

GlobusXIODefineDriver(
    test,
    &globus_i_xio_test_module,
    globus_l_xio_test_transport_load,
    globus_l_xio_test_transport_unload);
