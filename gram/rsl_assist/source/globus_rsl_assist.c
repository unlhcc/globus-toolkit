/******************************************************************************
 * globus_rsl_assist.c
 *
 * Description:
 *   The rsl_assist library provide a set of functions to help working with
 *   an RSL.
 *   It also contains some function to access the MDS; those function will be
 *   moved to a new library "mds_assist" in future release of GLOBUS.
 *   
 * CVS Information:
 *
 *   $Source$
 *   $Date$
 *   $Revision$
 *   $Author$
 *****************************************************************************/
#include "globus_common.h"
#include "globus_libc_setenv.h"
#include "globus_rsl_assist.h"
#include "globus_i_rsl_assist.h"

#include <string.h>


#if !defined(PATH_MAX) && defined(MAXPATHLEN)
#   define PATH_MAX MAXPATHLEN 
#endif

#include "lber.h"
#include "ldap.h"

/******************************************************************************
forward declarations 
******************************************************************************/

static 
int
globus_l_rsl_assist_simple_query_ldap(
    char * attribute,
    int    maximum,
    char * search_format,
    globus_list_t ** job_contact_list);

/*
 * Function: globus_rsl_assist_replace_manager_name()
 *
 * Uses the Globus RSL library and the UMich LDAP
 * library to modify an RSL specification, changing instances of
 *
 * resourceManagerName=x
 *
 * with
 *
 * resourceManagerContact=y
 *
 * where y is obtained by querying the MDS ldap server, searching
 * for an object which matches the following filter
 *
 *   (&(objectclass=GlobusResourceManager)(cn=x))
 *
 * and extracting the contact value for that object.
 * 
 * Parameters: 
 *     rsl - Pointer to the RSL structure in which you want to
 *           replace the manager Name by its Contact.
 *
 *     NOTE: The RSL MUST have been created using globus_rsl_parse, because
 *     the rsl might be reallocated by this function !! (required when
 *     the rsl is only a simple relation equal : resourceManagerName=x
 *
 * Returns:
 *     Pointer to the new RSL (Might be equal to the original one) or
 *     GLOBUS_NULL in case of failure
 *     
 */
globus_rsl_t *
globus_rsl_assist_replace_manager_name(
    globus_rsl_t * rsl)
{
    
    int                         rc;
    globus_list_t *		lists=GLOBUS_NULL;
    globus_rsl_t *              an_rsl;
    globus_rsl_t *              new_rsl;
    
    /*
     * if the request is a multirequest, run this function repeatedly 
     * over the list of requests
     */
    if (globus_rsl_is_boolean_multi(rsl))
    {
	lists = (globus_list_t *) globus_rsl_boolean_get_operand_list(rsl);
	while (!globus_list_empty(lists))
	{
	    an_rsl=globus_list_first(lists);
	    new_rsl=globus_rsl_assist_replace_manager_name(an_rsl);
	    if (new_rsl!=an_rsl)
	    {
		globus_list_replace_first(lists,new_rsl);
	    }
	    lists=globus_list_rest(lists);
	}
	return rsl;
    }

    if(globus_rsl_is_boolean(rsl))
    {

	/* get the operands of the boolean operator, in the example
	 * of &(foo=x)(bar=y)
	 * this would be the list containing the relations
	 *   foo=x
	 *   bar=y
	 */
	lists = globus_rsl_boolean_get_operand_list(rsl);

	/* look at each operand of the boolean, and figure out if
	 * it is a nested boolean, or a relation (x=y)
	 */
	while(!globus_list_empty(lists))
	{
	    globus_rsl_t *	head;

	    head = globus_list_first(lists);

	    /* if boolean, recursively process the request */
	    if(globus_rsl_is_boolean(head))
	    {
		
		new_rsl= globus_rsl_assist_replace_manager_name(head);
		if (new_rsl == GLOBUS_NULL)
		{
		    return GLOBUS_NULL;
		}
		if (head!=new_rsl)
		{
		    globus_list_replace_first(lists,new_rsl);
		}
	    }
	    /* if a relation, check to see if it's one we can deal with */
	    else if(globus_rsl_is_relation_eq(head))
	    {
		
		/* RSL attributes are case insensitive */
		if(strcasecmp(globus_rsl_relation_get_attribute(head),
			      "resourceManagerName") == 0)
		{
		    globus_rsl_value_t * value;
		    
		    /* The value of a relation may be a
		           single literal:  "foo"
		           substitution: $(SOMETHING)
			   list: ("foo" $(SOMETHING))
		       We only deal with single literals here. lists
		       don't make sense as the value for a
		       resourceManagerName.

		       They are always stored as a sequence, but
		       the globus_rsl_relation_get_single_value
		       function will pull out the value of a single
		       literal
		     */
		    value = globus_rsl_relation_get_single_value(head);
		    
		    if(value == NULL)
		    {
			/* ill-formed RSL, abort */
			return GLOBUS_NULL;
		    }
		    else if(!globus_rsl_value_is_literal(value))
		    {
		        /* don't process substitutions */
			return GLOBUS_NULL;
		    }
		    else
		    {
			char * resource_name;
			char * resource_contact;
			globus_rsl_value_t * resource_contact_value;
			globus_rsl_t * resource_contact_relation;
			globus_list_t * sequence = GLOBUS_NULL;

			resource_name =
			    globus_rsl_value_literal_get_string(value);

			resource_contact =
			    globus_i_rsl_assist_get_rm_contact(resource_name);
			   
			if (resource_contact == GLOBUS_NULL)
			{
			    
			    return GLOBUS_NULL;			    
			}

			/* make that into a sequence of a single literal
			 * remember that values are always sequences
			 */
			resource_contact_value = 
			    globus_rsl_value_make_literal(resource_contact);
			globus_list_insert(&sequence,
					   resource_contact_value);

			/* make a relation out of the desired attribute,
			 * and the new value:
			 * resourceManagerContact=<result>
		         */
			resource_contact_relation = 
			    globus_rsl_make_relation(
				GLOBUS_RSL_EQ,
				"resourceManagerContact",
				globus_rsl_value_make_sequence(
				    sequence));

			/* remove this node from the list of operands
			 * to the boolean
		         */
			
			globus_list_remove(
			    globus_rsl_boolean_get_operand_list_ref(rsl),
			    lists);
			globus_rsl_free_recursive(head);

			/* insert our new relation into the list */
			globus_list_insert(
			    globus_rsl_boolean_get_operand_list_ref(rsl),
					   (void *)resource_contact_relation);
		    }
		}
	    }
	    lists = globus_list_rest(lists);
	}
	return rsl;
    }
    else if (globus_rsl_is_relation_eq(rsl))
    {
	globus_rsl_t *	head=rsl;
	
	/* not boolean: relation x=y */
	/* RSL attributes are case insensitive */
	if(strcasecmp(globus_rsl_relation_get_attribute(head),
		      "resourceManagerName") == 0)
	{
	    globus_rsl_value_t * value;
	    
	    /* The value of a relation may be a
	       single literal:  "foo"
	       substitution: $(SOMETHING)
	       list: ("foo" $(SOMETHING))
	       We only deal with single literals here. lists
	       don't make sense as the value for a
	       resourceManagerName.
	       
	       They are always stored as a sequence, but
	       the globus_rsl_relation_get_single_value
	       function will pull out the value of a single
	       literal
	       */
	    value = globus_rsl_relation_get_single_value(head);
	    
	    if(value == GLOBUS_NULL)
	    {
		/* ill-formed RSL, abort */
		return GLOBUS_NULL;
	    }
	    else if(!globus_rsl_value_is_literal(value))
	    {
		/* don't process substitutions */
		return GLOBUS_NULL;;
	    }
	    else
	    {
		char * resource_name;
		char * resource_contact;
		globus_rsl_value_t * resource_contact_value;
		globus_rsl_t * resource_contact_relation;
		globus_list_t * sequence = GLOBUS_NULL;
		
		resource_name =
		    globus_rsl_value_literal_get_string(value);
		
		resource_contact =
		    globus_i_rsl_assist_get_rm_contact(resource_name);
		
		if (resource_contact == GLOBUS_NULL)
		{
		    
		    return GLOBUS_NULL;
		}
		
		/* make that into a sequence of a single literal
		 * remember that values are always sequences
		 */
		resource_contact_value = 
		    globus_rsl_value_make_literal(resource_contact);
		globus_list_insert(&sequence,
				   resource_contact_value);
		
		/* make a relation out of the desired attribute,
		 * and the new value:
		 * resourceManagerContact=<result>
		 */
		resource_contact_relation = 
		    globus_rsl_make_relation(
			GLOBUS_RSL_EQ,
			"resourceManagerContact",
			globus_rsl_value_make_sequence(
			    sequence));
		/* free the original RSL and return the new one...*/
		globus_rsl_free_recursive(rsl);
		return resource_contact_relation;
	    }
	}
	return rsl;
    }
    return GLOBUS_NULL;
} /* globus_rsl_assist_replace_manager_name() */


/*
 * Function: globus_l_rsl_assist_query_ldap()
 *
 * Connect to the ldap server, and return all the value of the
 * fields "attribute" contained in the entry maching a search string.
 *
 * Parameters:
 *     attribute -     field for which we want the list of value returned.
 *     maximum -       maximum number of string returned in the list.
 *     search_string - Search string to use to select the entry for which
 *                     we will return the values of the field attribute.
 * 
 * Returns:
 *     a list of string containing the result.
 *     
 */ 
static
int
globus_l_rsl_assist_simple_query_ldap(
    char * attribute,
    int    maximum,
    char * search_string,
    globus_list_t ** job_contact_list)
{
    LDAP *			ldap_server;
    int				port;
    char *			base_dn=GLOBUS_MDS_ROOT_DN;
    char *			server;
    LDAPMessage *		reply;
    LDAPMessage *		entry;
    char *			attrs[3];
    int decrement;

    
    *job_contact_list = GLOBUS_NULL;

    if (port=globus_libc_getenv("GLOBUS_MDS_PORT") == GLOBUS_NULL)
    {
	port=atoi(GLOBUS_MDS_PORT);
    }
    if (server=globus_libc_getenv("GLOBUS_MDS_HOST") == GLOBUS_NULL)
    {
	server=GLOBUS_MDS_HOST;
    }
    /* connect to the ldap server */
    if((ldap_server = ldap_open(server, port)) == GLOBUS_NULL)
    {
	ldap_perror(ldap_server, "ldap_open");
	return -1;
    }

    /* bind anonymously (we can only read public records now */
    if(ldap_simple_bind_s(ldap_server, "", "") != LDAP_SUCCESS)
    {
	ldap_perror(ldap_server, "ldap_simple_bind_s");
	ldap_unbind(ldap_server);
	return -1;
    }

    
    /* I should verify the attribute is a valid string...       */
    /* the function allow only one  attribute to be return */
    attrs[0] = attribute;
    attrs[1] = GLOBUS_NULL;
    
    
    /* do a synchronous search of the entire ldap tree,
     * and return the desired attribute
     */
    if(ldap_search_s(ldap_server,
		     base_dn,
		     LDAP_SCOPE_SUBTREE,
		     search_string,
		     attrs,
		     0,
		     &reply) != LDAP_SUCCESS)
    {
	ldap_perror(ldap_server, "ldap_search");
	ldap_unbind(ldap_server);
	return -1;
    }

    if (maximum ==-1)
    {
	decrement=0;
    }
    else
    {
	decrement=1;
    }
    for(entry = ldap_first_entry(ldap_server, reply);
	entry != GLOBUS_NULL && maximum;
	entry = ldap_next_entry(ldap_server, entry), maximum-=decrement)
    {
	char *attr_value;
	char *a;
	BerElement *ber;
	int numValues;
	char** values;
	int i;
	char *desired_attribute_value=GLOBUS_NULL;
	
	for (a = ldap_first_attribute(ldap_server,entry,&ber); a != NULL;
	     a = ldap_next_attribute(ldap_server,entry,ber) )
	{
	    
	    /* got our match, so copy and return it*/
	    if(strcmp(a, attrs[0]) == 0)
	    {
		values = ldap_get_values(ldap_server,entry,a);
		numValues = ldap_count_values(values);
		for (i=0; i<numValues; i++)
		{
		    attr_value = strdup(values[i]);
		    globus_list_insert(job_contact_list,attr_value);
		    /*globus_libc_printf("ONEENTRY %d %s \n",
		      numValues,attr_value);*/
		    maximum-=decrement;
		    if (maximum==0)
		    {
			break;
		    }
		}
		ldap_value_free(values);
		
		/* we never have 2 time the same attibute for the same entry */
		break;
	    }
	}

    }
    /* disconnect from the server */
    ldap_unbind(ldap_server);
    /* to avoid a leak in ldap code */
    ldap_msgfree(reply);
    return GLOBUS_SUCCESS;
} /* globus_l_rsl_assist_simple_query_ldap() */



/*
 * Function: globus_i_rsl_assist_get_job_contact_list()
 *
 * Connect to the ldap server, and search for the contact string
 * associated with the resourceManagerName.
 *
 * Parameters: 
 * 
 * Returns: 
 */ 
int
globus_i_rsl_assist_get_job_contact_list(globus_list_t ** job_contact_list)
{

    int rc;
    
    char * search_string=
	"(&(objectclass=GlobusResourceManager))";

    return globus_l_rsl_assist_simple_query_ldap(
	"scheduledjob",
	-1,
	search_string,
	job_contact_list);

} /* globus_l_rsl_assist_get_job_contact_list() */


/*
 * Function: globus_rsl_assist_get_rm_contact()
 *
 * Connect to the ldap server, and search for the contact string
 * associated with the resourceManagerName, by querying the MDS.
 *
 * For the moment, just a wrapper around globus_l_rsl_assist_query_ldap(),
 * until globus_l_rsl_assist_query_ldap(), get more general...
 *
 * Parameters:
 *    resourceManagerName - String containing the Name of the Resource Manager
 *
 * Returns:
 *    Pointer to a newly allocated string containing the Resource
 *    Manager Contact. This string MUST be freed by the user.
 *    OR
 *    GLOBUS_NULL in case of failure.
 */
char*
globus_i_rsl_assist_get_rm_contact(
    char* resource)
{
    int    rc;
    char * search_string;
    char * search_format=
	"(&(objectclass=GlobusResourceManager)"
	"(cn=%s))";
    globus_list_t * rm_contact_list=GLOBUS_NULL;
    char * result;
    
    search_string=globus_malloc(strlen(resource)+
				strlen(search_format)+
				1);
    if (search_string==GLOBUS_NULL)
    {
	return GLOBUS_NULL;
    }
    sprintf(search_string, search_format, resource);

    rc = globus_l_rsl_assist_simple_query_ldap(
	"contact",
	1,
	search_string,
	&rm_contact_list);
    if (rc != GLOBUS_SUCCESS || rm_contact_list == GLOBUS_NULL)
    {
	globus_libc_free(search_string);
	return GLOBUS_NULL;
    }
    result=globus_list_remove(&rm_contact_list, rm_contact_list);
    
    globus_libc_free(search_string);
    return result;
    
} /* globus_rsl_assist_get_rm_contact() */

