#include "globus_extension.h"
#include "globus_debug.h"
#include GLOBUS_THREAD_INCLUDE
#include "globus_thread_rmutex.h"
#include "globus_libc.h"
#include "ltdl.h"

/* provides local_version and build_flavor */
#include "version.h"

#ifdef WIN32
#define MY_LIB_EXT ".dll"
#else
#define MY_LIB_EXT ".so"
#endif

GlobusDebugDefine(GLOBUS_EXTENSION);

#define GlobusExtensionDebugPrintf(level, message)                          \
    GlobusDebugPrintf(GLOBUS_EXTENSION, level, message)

#define GlobusExtensionDebugEnter()                                         \
    GlobusExtensionDebugPrintf(                                             \
        GLOBUS_L_EXTENSION_DEBUG_TRACE,                                     \
        ("[%s] Entering\n", _globus_func_name))

#define GlobusExtensionDebugExit()                                          \
    GlobusExtensionDebugPrintf(                                             \
        GLOBUS_L_EXTENSION_DEBUG_TRACE,                                     \
        ("[%s] Exiting\n", _globus_func_name))

#define GlobusExtensionDebugExitWithError()                                 \
    GlobusExtensionDebugPrintf(                                             \
        GLOBUS_L_EXTENSION_DEBUG_TRACE,                                     \
        ("[%s] Exiting with error\n", _globus_func_name))

#define GlobusExtensionDebugEnterSymbol(symbol)                             \
    GlobusExtensionDebugPrintf(                                             \
        GLOBUS_L_EXTENSION_DEBUG_TRACE,                                     \
        ("[%s] Entering (%s)\n", _globus_func_name, (symbol)))

enum globus_l_extension_debug_levels
{
    GLOBUS_L_EXTENSION_DEBUG_TRACE      = 1,
    GLOBUS_L_EXTENSION_DEBUG_VERBOSE    = 2,
    GLOBUS_L_EXTENSION_DEBUG_DLL        = 4
};

typedef struct globus_l_extension_module_s
{
    char *                              name;
    long                                ref;
    long                                module_ref;
    globus_module_descriptor_t *        module;
    lt_dlhandle                         dlhandle;
    struct globus_l_extension_module_s *owner;
} globus_l_extension_module_t;

typedef struct
{
    char *                              extension_name;
    globus_module_descriptor_t *        module;
    globus_l_extension_module_t *       owner;
} globus_l_extension_builtin_t;

typedef struct globus_l_extension_handle_s
{
    globus_l_extension_module_t *       owner;
    globus_module_descriptor_t *        module;
    char *                              symbol;
    void *                              datum;
    long                                ref;
} globus_l_extension_handle_t;

static globus_thread_key_t              globus_l_extension_owner_key;
static globus_thread_key_t              globus_l_libtool_key;
static globus_rmutex_t                  globus_l_libtool_mutex;
static globus_rmutex_t                  globus_l_extension_mutex;
static globus_hashtable_t               globus_l_extension_loaded;
static globus_hashtable_t               globus_l_extension_builtins;
/*
static globus_hashtable_t               globus_l_extension_mappings;
*/

static
void
globus_l_libtool_mutex_lock(void)
{
    globus_rmutex_lock(&globus_l_libtool_mutex);
}

static
void
globus_l_libtool_mutex_unlock(void)
{
    globus_rmutex_unlock(&globus_l_libtool_mutex);
}

static
void
globus_l_libtool_set_error(
    const char *                        error)
{
    globus_thread_setspecific(globus_l_libtool_key, (void *) error);
}

static
const char *
globus_l_libtool_get_error(void)
{
    return (char *) globus_thread_getspecific(globus_l_libtool_key);
}

/**
 * load config file.  substitute $BUILD_FLAVOR for build_flavor,
 */
static
int
globus_l_extension_activate(void)
{
    static globus_bool_t                initialized = GLOBUS_FALSE;
    GlobusFuncName(globus_l_extension_activate);
    
    if(!initialized)
    {
        GlobusDebugInit(GLOBUS_EXTENSION, TRACE VERBOSE DLL);
        GlobusExtensionDebugEnter();
    
        globus_rmutex_init(&globus_l_libtool_mutex, NULL);
        globus_thread_key_create(&globus_l_libtool_key, NULL);
        
        if(lt_dlinit() != 0)
        {
            goto error_dlinit;
        }
        if(lt_dlmutex_register(
            globus_l_libtool_mutex_lock,
            globus_l_libtool_mutex_unlock,
            globus_l_libtool_set_error,
            globus_l_libtool_get_error) != 0)
        {
            goto error_dlmutex;
        }
        
        globus_hashtable_init(
            &globus_l_extension_loaded,
            32,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
        globus_hashtable_init(
            &globus_l_extension_builtins,
            32,
            globus_hashtable_string_hash,
            globus_hashtable_string_keyeq);
            
        globus_rmutex_init(&globus_l_extension_mutex, NULL);
        globus_thread_key_create(&globus_l_extension_owner_key, NULL);
        
        initialized = GLOBUS_TRUE;
        GlobusExtensionDebugExit();
    }
    
    return GLOBUS_SUCCESS;

error_dlmutex:
    lt_dlexit();
error_dlinit:
    GlobusExtensionDebugExitWithError();
    return GLOBUS_FAILURE;
}

/**
 * called locked 
 * 
 * dont call until ref goes to 0 or proxy deavtivate is called
 * (circumventing my deactivate procedure)
 */
static
void
globus_l_extension_shutdown_extension(
    globus_l_extension_module_t *       extension,
    globus_bool_t                       in_proxy)
{
    globus_module_descriptor_t *        module;
    GlobusFuncName(globus_l_extension_shutdown_extension);
    
    GlobusExtensionDebugEnter();
    
    if(!in_proxy)
    {
        if(extension->module)
        {
            /* this will cause this function to be re-entered to do the 
             * bottom half
             */
            globus_rmutex_unlock(&globus_l_extension_mutex);
            globus_module_deactivate(extension->module);
            globus_rmutex_lock(&globus_l_extension_mutex);
            return;
        }
    }
    else if(extension->module)
    {
        module = extension->module;
        extension->module = NULL;
        globus_hashtable_remove(
            &globus_l_extension_loaded, extension->name);
        if(module->deactivation_func)
        {
            globus_rmutex_unlock(&globus_l_extension_mutex);
            module->deactivation_func();
            globus_rmutex_lock(&globus_l_extension_mutex);
        }
    }
    
    /** if this is not zero, then we must have been called by the deactivate
     * proxy (on behalf of globus_module_deactivate_all() or someone getting
     * a hold of our module descriptor and deactivating it themself
     * 
     * this will be non-zero if users still have outstanding
     * references on our registries when that circumvented deactivate occurred.
     */
    if(extension->ref == 0)
    {
        if(extension->dlhandle)
        {
            /** cant do this until i provide a way for callbacks to be
             * wrapped and hold a reference on this.  from the xio TODO:
             * - extension code needs to reference count callbacks to prevent
             *   modules from being unloaded.  there is no way for the user to
             *   protect itself from this. this ref count does not need to
             *   block module deactivation, only the dlclose(). probably also
             *   need a register_oneshot wrapper function that can do this.
             * -- for now, extensions are just never unloaded.
             *
            lt_dlclose(extension->dlhandle);
             */
        }
        globus_free(extension->name);
        globus_free(extension);
    }
    
    GlobusExtensionDebugExit();
}

static
int
globus_l_extension_deactivate_proxy(
    globus_module_descriptor_t *        module,
    void *                              user_arg)
{
    globus_l_extension_module_t *       extension;
    GlobusFuncName(globus_l_extension_deactivate_proxy);
    
    GlobusExtensionDebugEnter();
    extension = (globus_l_extension_module_t *) user_arg;
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        extension->ref -= extension->module_ref;
        extension->module_ref = 0;
        globus_l_extension_shutdown_extension(extension, GLOBUS_TRUE);
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
    return GLOBUS_SUCCESS;
}

static
lt_dlhandle
globus_l_extension_dlopen(
    const char *                        name)
{
    char                                library[1024];
    lt_dlhandle                         dlhandle;
    
    snprintf(library, 1024, "lib%s_%s", name, build_flavor);
    library[1023] = 0;
    dlhandle = lt_dlopenext(library);
    if(!dlhandle)
    {
        /* older libtools dont search the extensions correctly */
        snprintf(library, 1024, "lib%s_%s" MY_LIB_EXT, name, build_flavor);
        library[1023] = 0;
        dlhandle = lt_dlopenext(library);
        if(!dlhandle)
        {
            const char *                error;
            
            error = lt_dlerror();
            
            GlobusExtensionDebugPrintf(
                GLOBUS_L_EXTENSION_DEBUG_DLL,
                ("Couldn't dlopen %s: %s\n",
                    library, error ? error : "(null)"));
        }
    }
    
    return dlhandle;
}

static
globus_module_descriptor_t *
globus_l_extension_get_module(
    lt_dlhandle                         dlhandle)
{
    globus_module_descriptor_t *        module;
    
    module = (globus_module_descriptor_t *)
        lt_dlsym(dlhandle, "globus_extension_module");
    if(!module)
    {
        const char *                    error;
        
        error = lt_dlerror();
        
        GlobusExtensionDebugPrintf(
            GLOBUS_L_EXTENSION_DEBUG_DLL,
            ("Couldn't find module descriptor : %s\n",
            error ? error : "(null)"));
    }
    
    return module;
}

int
globus_extension_activate(
    const char *                        extension_name)
{
    globus_l_extension_module_t *       extension;
    globus_l_extension_module_t *       last_extension;
    globus_l_extension_builtin_t *      builtin;
    int                                 rc;
    GlobusFuncName(globus_extension_activate);
    
    GlobusExtensionDebugEnterSymbol(extension_name);
    
    if(!extension_name)
    {
        goto error_param;
    }
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        extension = (globus_l_extension_module_t *)
            globus_hashtable_lookup(
                &globus_l_extension_loaded, (void *) extension_name);
        if(!extension)
        {
            extension = (globus_l_extension_module_t *)
                globus_malloc(sizeof(globus_l_extension_module_t));
            if(!extension)
            {
                goto error_alloc;
            }
            
            extension->module_ref = 1;
            extension->ref = 1;
            extension->name = globus_libc_strdup(extension_name);
            if(!extension->name)
            {
                goto error_strdup;
            }
            
            builtin = (globus_l_extension_builtin_t *)
                globus_hashtable_lookup(
                    &globus_l_extension_builtins, (void *) extension_name);
            if(builtin && (!builtin->owner || builtin->owner->module_ref > 0))
            {
                extension->dlhandle = NULL;
                extension->module = builtin->module;
                extension->owner = builtin->owner;
                if(extension->owner)
                {
                    extension->owner->ref++;
                }
            }
            else
            {
                extension->owner = NULL;
                extension->dlhandle =   
                    globus_l_extension_dlopen(extension->name);
                if(!extension->dlhandle)
                {
                    goto error_dll;
                }
                
                extension->module = (globus_module_descriptor_t *) 
                    globus_l_extension_get_module(extension->dlhandle);
                if(!extension->module)
                {
                    goto error_module;
                }
            }
            
            globus_hashtable_insert(
                &globus_l_extension_loaded,
                extension->name,
                extension);
                
            last_extension = (globus_l_extension_module_t *)
                globus_thread_getspecific(globus_l_extension_owner_key);
            globus_thread_setspecific(globus_l_extension_owner_key, extension);
            
            rc = globus_module_activate_proxy(
                extension->module,
                globus_l_extension_deactivate_proxy,
                extension);
            
            globus_thread_setspecific(
                globus_l_extension_owner_key, last_extension);
            if(rc != GLOBUS_SUCCESS)
            {
                goto error_activate;
            }
        }
        else
        {
            extension->module_ref++;
            extension->ref++;
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
    return GLOBUS_SUCCESS;

error_activate:
    globus_hashtable_remove(
        &globus_l_extension_loaded, extension->name);
    if(builtin && builtin->owner)
    {
        builtin->owner->ref--;
    }
error_module:
    if(extension->dlhandle)
    {
        lt_dlclose(extension->dlhandle);
    }
error_dll:
    globus_free(extension->name);
error_strdup:
    globus_free(extension);
error_alloc:
    globus_rmutex_unlock(&globus_l_extension_mutex);
error_param:
    GlobusExtensionDebugExitWithError();
    return GLOBUS_FAILURE;
}

int
globus_extension_deactivate(
    const char *                        extension_name)
{
    globus_l_extension_module_t *       extension;
    globus_l_extension_module_t *       owner = NULL;
    GlobusFuncName(globus_extension_deactivate);
    
    GlobusExtensionDebugEnterSymbol(extension_name);
    
    if(!extension_name)
    {
        goto error_param;
    }
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        extension = (globus_l_extension_module_t *)
            globus_hashtable_lookup(
                &globus_l_extension_loaded, (void *) extension_name);
        if(!extension || extension->module_ref <= 0)
        {
            goto error_lookup;
        }
        
        extension->module_ref--;
        if(--extension->ref == 0)
        {
            if(extension->owner && --extension->owner->ref == 0)
            {
                owner = extension->owner;
            }

            globus_l_extension_shutdown_extension(extension, GLOBUS_FALSE);
            
            if(owner)
            {
                globus_l_extension_shutdown_extension(owner, GLOBUS_FALSE);
            }
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
    return GLOBUS_SUCCESS;

error_lookup:
    globus_rmutex_unlock(&globus_l_extension_mutex);
error_param:
    GlobusExtensionDebugExitWithError();
    return GLOBUS_FAILURE;
}

int
globus_extension_registry_add(
    globus_extension_registry_t *       registry,
    const char *                        symbol,
    globus_module_descriptor_t *        module,
    void *                              data)
{
    globus_l_extension_handle_t *       entry;
    GlobusFuncName(globus_extension_registry_add);
    
    GlobusExtensionDebugEnterSymbol(symbol);
    
    if(!data || !symbol || !registry)
    {
        goto error_params;
    }
    
    entry = (globus_l_extension_handle_t *)
        globus_malloc(sizeof(globus_l_extension_handle_t));
    if(!entry)
    {
        goto error_malloc;
    }
    
    entry->owner = (globus_l_extension_module_t *)
        globus_thread_getspecific(globus_l_extension_owner_key);
    entry->module = module;
    entry->datum = data;
    entry->ref = 1;
    entry->symbol = globus_libc_strdup(symbol);
    if(!entry->symbol)
    {
        goto error_dup;
    }
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        if(!registry->initialized)
        {
            if(globus_hashtable_init(
                &registry->table,
                20,
                globus_hashtable_string_hash,
                globus_hashtable_string_keyeq) != GLOBUS_SUCCESS)
            {
                goto error_init;
            }
            
            registry->initialized = GLOBUS_TRUE;
        }
        
        if(globus_hashtable_insert(
            &registry->table, entry->symbol, entry) != GLOBUS_SUCCESS)
        {
            goto error_insert;
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
    return GLOBUS_SUCCESS;

error_insert:
error_init:
    globus_rmutex_unlock(&globus_l_extension_mutex);
    globus_free(entry->symbol);
error_dup:
    globus_free(entry);
error_malloc:
error_params:
    GlobusExtensionDebugExitWithError();
    return GLOBUS_FAILURE;
}

void *
globus_extension_registry_remove(
    globus_extension_registry_t *       registry,
    const char *                        symbol)
{
    globus_l_extension_handle_t *       entry;
    void *                              datum = NULL;
    GlobusFuncName(globus_extension_registry_remove);
    
    GlobusExtensionDebugEnterSymbol(symbol);
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        if(registry->initialized)
        {
            entry = (globus_l_extension_handle_t *)
                globus_hashtable_lookup(&registry->table, (void *) symbol);
            if(entry && entry->datum)
            {
                datum = entry->datum;
                globus_hashtable_remove(&registry->table, (void *) symbol);
                if(--entry->ref == 0)
                {
                    globus_free(entry->symbol);
                    globus_free(entry);
                }
            }
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
    return datum;
}

void *
globus_extension_lookup(
    globus_extension_handle_t *         handle,
    globus_extension_registry_t *       registry,
    const char *                        symbol)
{
    globus_l_extension_handle_t *       entry;
    void *                              datum = NULL;
    GlobusFuncName(globus_extension_lookup);
    
    GlobusExtensionDebugEnterSymbol(symbol);
    
    if(!handle)
    {
        goto error_param;
    }
    
    *handle = NULL;
    if(!registry || !symbol)
    {
        goto error_param;
    }
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        if(registry->initialized)
        {
            entry = (globus_l_extension_handle_t *)
                globus_hashtable_lookup(&registry->table, (void *) symbol);
            if(entry && (!entry->owner || entry->owner->module_ref > 0))
            {
                datum = entry->datum;
                entry->ref++;
                if(entry->owner)
                {
                    entry->owner->ref++;
                  
                    GlobusExtensionDebugPrintf(
                        GLOBUS_L_EXTENSION_DEBUG_VERBOSE,
                        ("Accessing symbol %s within %s\n",
                            symbol, entry->owner->name));
                }
                
                *handle = entry;
            }
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
    return datum;

error_param:
    GlobusExtensionDebugExitWithError();
    return NULL;
}

void
globus_extension_release(
    globus_extension_handle_t           handle)
{
    globus_l_extension_handle_t *       entry;
    globus_l_extension_module_t *       owner = NULL;
    GlobusFuncName(globus_extension_release);
    
    GlobusExtensionDebugEnterSymbol(handle->symbol);
    
    entry = handle;
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        if(entry)
        {
            if(entry->owner && --entry->owner->ref == 0)
            {
                owner = entry->owner;
            }
            
            if(--entry->ref == 0)
            {
                globus_free(entry->symbol);
                globus_free(entry);
            }
            
            if(owner)
            {
                globus_l_extension_shutdown_extension(owner, GLOBUS_FALSE);
            }
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
}

globus_bool_t
globus_extension_error_match(
    globus_extension_handle_t           handle,
    globus_object_t *                   error,
    int                                 type)
{
    globus_bool_t                       match = GLOBUS_FALSE;
    GlobusFuncName(globus_extension_error_match);
    
    GlobusExtensionDebugEnter();
    
    if(!handle || !error)
    {
        goto error_param;
    }
    
    match = globus_error_match(error, handle->module, type);
    
    GlobusExtensionDebugExit();
    return match;

error_param:
    GlobusExtensionDebugExitWithError();
    return GLOBUS_FALSE;
}

int
globus_extension_register_builtin(
    const char *                        extension_name,
    globus_module_descriptor_t *        module_descriptor)
{
    globus_l_extension_builtin_t *      builtin;
    GlobusFuncName(globus_extension_register_builtin);
    
    GlobusExtensionDebugEnterSymbol(extension_name);
    
    builtin = (globus_l_extension_builtin_t *)
        globus_malloc(sizeof(globus_l_extension_builtin_t));
    if(!builtin)
    {
        goto error_alloc;
    }
    
    builtin->owner = (globus_l_extension_module_t *)
        globus_thread_getspecific(globus_l_extension_owner_key);
    builtin->module = module_descriptor;
    builtin->extension_name = globus_libc_strdup(extension_name);
    if(!builtin->extension_name)
    {
        goto error_strdup;
    }
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        int                             rc;
        
        rc = globus_hashtable_insert(
            &globus_l_extension_builtins, builtin->extension_name, builtin);
        if(rc != 0)
        {
            goto error_insert;
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
    return GLOBUS_SUCCESS;

error_insert:
    globus_rmutex_unlock(&globus_l_extension_mutex);
    globus_free(builtin->extension_name);
error_strdup:
    globus_free(builtin);
error_alloc:
    GlobusExtensionDebugExitWithError();
    return GLOBUS_FAILURE;
}

void
globus_extension_unregister_builtin(
    const char *                        extension_name)
{
    globus_l_extension_builtin_t *      builtin;
    GlobusFuncName(globus_extension_unregister_builtin);
    
    GlobusExtensionDebugEnterSymbol(extension_name);
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        builtin = (globus_l_extension_builtin_t *)
            globus_hashtable_remove(
                &globus_l_extension_builtins, (void *) extension_name);
        if(builtin)
        {
            globus_free(builtin->extension_name);
            globus_free(builtin);
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
}

int
globus_extension_register_builtins(
    globus_extension_builtin_t *        builtins)
{
    int                                 i;
    GlobusFuncName(globus_extension_register_builtins);
    
    GlobusExtensionDebugEnter();
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        for(i = 0; builtins[i].extension_name; i++)
        {
            int                         rc;
            
            rc = globus_extension_register_builtin(
                builtins[i].extension_name, builtins[i].module_descriptor);
            if(rc != 0)
            {
                goto error_register;
            }
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
    return GLOBUS_SUCCESS;
    
error_register:
    while(i--)
    {
        globus_extension_unregister_builtin(builtins->extension_name);
        builtins++;
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    return GLOBUS_FAILURE;
}

void
globus_extension_unregister_builtins(
    globus_extension_builtin_t *        builtins)
{
    GlobusFuncName(globus_extension_unregister_builtins);
    
    GlobusExtensionDebugEnter();
    
    globus_rmutex_lock(&globus_l_extension_mutex);
    {
        while(builtins->extension_name)
        {
            globus_extension_unregister_builtin(builtins->extension_name);
            builtins++;
        }
    }
    globus_rmutex_unlock(&globus_l_extension_mutex);
    
    GlobusExtensionDebugExit();
}

globus_module_descriptor_t              globus_i_extension_module =
{
    "globus_extension_module",
    globus_l_extension_activate,
    NULL,
    NULL,
    NULL,
    &local_version
};
