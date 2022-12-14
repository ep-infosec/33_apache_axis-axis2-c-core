/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <axis2_disp.h>
#include "axis2_disp_checker.h"
#include <axis2_conf.h>
#include <axutil_dir_handler.h>
#include <axis2_dep_engine.h>
#include <axis2_arch_reader.h>
#include <axis2_core_utils.h>

struct axis2_conf
{
    axutil_hash_t *svc_grps;
    axis2_transport_in_desc_t *transports_in[AXIS2_TRANSPORT_ENUM_MAX];
    axis2_transport_out_desc_t *transports_out[AXIS2_TRANSPORT_ENUM_MAX];

    /**
     * All the modules already engaged can be found here.
     */
    axutil_array_list_t *engaged_module_list;

    /*To store all the available modules (including version) */
    axutil_hash_t *all_modules;

    /*To store mapping between default version to module name */
    axutil_hash_t *name_to_version_map;
    axutil_array_list_t *out_phases;
    axutil_array_list_t *in_fault_phases;
    axutil_array_list_t *out_fault_phases;

    /* All the system specific phases are stored here */
    axutil_array_list_t *in_phases_upto_and_including_post_dispatch;

    axis2_phases_info_t *phases_info;
    axutil_hash_t *all_svcs;
    axutil_hash_t *all_init_svcs;
    axutil_hash_t *msg_recvs;
    axutil_hash_t *faulty_svcs;
    axutil_hash_t *faulty_modules;
    axis2_char_t *axis2_repo;
    axis2_char_t *axis2_xml;
    axis2_dep_engine_t *dep_engine;
    axutil_array_list_t *handlers;
    axis2_bool_t enable_mtom;
    /*This is used in rampart */
    axis2_bool_t enable_security;

    /** Configuration parameter container */
    axutil_param_container_t *param_container;

    /** Base description struct */
    axis2_desc_t *base;

    /** Mark whether conf is built using axis2 XML*/
    axis2_bool_t axis2_flag;

#if 0
/* this seemed to be not used after 1.6.0 */
/* This is a hack to keep rampart_context at client side */
void *security_context;
#endif
};

AXIS2_EXTERN axis2_conf_t *AXIS2_CALL
axis2_conf_create(
    const axutil_env_t * env)
{
    axis2_conf_t *conf = NULL;
    axis2_status_t status = AXIS2_FAILURE;
    axis2_phase_t *phase = NULL;
    int i = 0;

    AXIS2_ENV_CHECK(env, NULL);

    conf = (axis2_conf_t *)AXIS2_MALLOC(env->allocator, sizeof(axis2_conf_t));
    if(!conf)
    {
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "No memory");
        return NULL;
    }

    memset((void *)conf, 0, sizeof(axis2_conf_t));

    conf->param_container = (axutil_param_container_t *)axutil_param_container_create(env);
    if(!conf->param_container)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating parameter container failed");
        return NULL;
    }

    conf->svc_grps = axutil_hash_make(env);
    if(!conf->svc_grps)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating service group map failed");
        return NULL;
    }

    for(i = 0; i < AXIS2_TRANSPORT_ENUM_MAX; i++)
    {
        conf->transports_in[i] = NULL;
    }

    for(i = 0; i < AXIS2_TRANSPORT_ENUM_MAX; i++)
    {
        conf->transports_out[i] = NULL;
    }

    conf->engaged_module_list = axutil_array_list_create(env, 0);
    if(!conf->engaged_module_list)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating engaged module list failed");
        return NULL;
    }

    conf->handlers = axutil_array_list_create(env, 0);
    if(!conf->handlers)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating handler list failed");
        return NULL;
    }

    conf->in_phases_upto_and_including_post_dispatch = axutil_array_list_create(env, 0);
    if(!conf->in_phases_upto_and_including_post_dispatch)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
            "Creating in phases list upto and including post dispatch failed");
        return NULL;
    }
    else
    {
        axis2_disp_t *uri_dispatch = NULL;
        axis2_disp_t *addr_dispatch = NULL;

        phase = axis2_phase_create(env, AXIS2_PHASE_TRANSPORT_IN);
        if(!phase)
        {
            axis2_conf_free(conf, env);
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating phase %s failed",
                AXIS2_PHASE_TRANSPORT_IN);
            return NULL;
        }

        /* In case of using security we need to find the service/operation parameters before the 
         * dispatch phase. This is required to give parameters to the security inflow.*/
        uri_dispatch = axis2_req_uri_disp_create(env);
        if(uri_dispatch)
        {
            axis2_handler_t *handler = NULL;
            handler = axis2_disp_get_base(uri_dispatch, env);
            axis2_disp_free(uri_dispatch, env);
            axis2_phase_add_handler_at(phase, env, 0, handler);
            axutil_array_list_add(conf->handlers, env, axis2_handler_get_handler_desc(handler, env));
        }

        addr_dispatch = axis2_addr_disp_create(env);
        if(addr_dispatch)
        {
            axis2_handler_t *handler = NULL;
            handler = axis2_disp_get_base(addr_dispatch, env);
            axis2_disp_free(addr_dispatch, env);
            axis2_phase_add_handler_at(phase, env, 1, handler);
            axutil_array_list_add(conf->handlers, env, axis2_handler_get_handler_desc(handler, env));
        }

        status
            = axutil_array_list_add(conf->in_phases_upto_and_including_post_dispatch, env, phase);
        if(AXIS2_SUCCESS != status)
        {
            axis2_conf_free(conf, env);
            axis2_phase_free(phase, env);
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
                "Adding phase %s into in phases upto and including post dispatch list failed",
                AXIS2_PHASE_TRANSPORT_IN);
            return NULL;
        }

        phase = axis2_phase_create(env, AXIS2_PHASE_PRE_DISPATCH);
        if(!phase)
        {
            axis2_conf_free(conf, env);
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating phase %s failed",
                AXIS2_PHASE_PRE_DISPATCH);
            return NULL;
        }

        status
            = axutil_array_list_add(conf->in_phases_upto_and_including_post_dispatch, env, phase);
        if(AXIS2_SUCCESS != status)
        {
            axis2_conf_free(conf, env);
            axis2_phase_free(phase, env);
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
                "Adding phase %s into in phases upto and including post dispatch list failed",
                AXIS2_PHASE_PRE_DISPATCH);
            return NULL;
        }
    }

    conf->all_svcs = axutil_hash_make(env);
    if(!conf->all_svcs)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating all services map failed");
        return NULL;
    }

    conf->all_init_svcs = axutil_hash_make(env);
    if(!conf->all_init_svcs)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating all init services map failed");
        return NULL;
    }

    conf->msg_recvs = axutil_hash_make(env);
    if(!conf->msg_recvs)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating message receivers map failed.");
        return NULL;
    }

    conf->faulty_svcs = axutil_hash_make(env);
    if(!conf->faulty_svcs)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating fault services map failed");
        return NULL;
    }

    conf->faulty_modules = axutil_hash_make(env);
    if(!conf->faulty_modules)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating fault modules map failed");
        return NULL;
    }

    conf->all_modules = axutil_hash_make(env);
    if(!conf->all_modules)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating all modules map failed");
        return NULL;
    }

    conf->name_to_version_map = axutil_hash_make(env);
    if(!conf->name_to_version_map)
    {
        axis2_conf_free(conf, env);
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating name to version map failed");
        return NULL;
    }

    conf->base = axis2_desc_create(env);
    if(!conf->base)
    {
        axis2_conf_free(conf, env);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
            "Creating Axis2 configuration base description failed");
        return NULL;
    }

    return conf;
}

AXIS2_EXTERN void AXIS2_CALL
axis2_conf_free(
    axis2_conf_t * conf,
    const axutil_env_t * env)
{
    int i = 0;

    if(!conf)
    {
        /* nothing to free */
        return;
    }

    if(conf->param_container)
    {
        axutil_param_container_free(conf->param_container, env);
    }

    if(conf->svc_grps)
    {
        axutil_hash_index_t *hi = NULL;
        void *val = NULL;
        for(hi = axutil_hash_first(conf->svc_grps, env); hi; hi = axutil_hash_next(env, hi))
        {
            axis2_svc_grp_t *svc_grp = NULL;
            axutil_hash_this(hi, NULL, NULL, &val);
            svc_grp = (axis2_svc_grp_t *)val;
            if(svc_grp)
            {
                axis2_svc_grp_free(svc_grp, env);
            }
        }
        axutil_hash_free(conf->svc_grps, env);
    }

    for(i = 0; i < AXIS2_TRANSPORT_ENUM_MAX; i++)
    {
        if(conf->transports_in[i])
        {
            axis2_transport_in_desc_free(conf->transports_in[i], env);
        }
    }

    for(i = 0; i < AXIS2_TRANSPORT_ENUM_MAX; i++)
    {
        if(conf->transports_out[i])
        {
            axis2_transport_out_desc_free(conf->transports_out[i], env);
        }
    }

    if(conf->dep_engine)
    {
        axis2_dep_engine_free(conf->dep_engine, env);
    }

    if(conf->all_modules)
    {
        axutil_hash_index_t *hi = NULL;
        void *val = NULL;
        for(hi = axutil_hash_first(conf->all_modules, env); hi; hi = axutil_hash_next(env, hi))
        {
            axis2_module_desc_t *module_desc = NULL;
            axutil_hash_this(hi, NULL, NULL, &val);
            module_desc = (axis2_module_desc_t *)val;
            if(module_desc)
            {
                axis2_module_desc_free(module_desc, env);
            }
        }
        axutil_hash_free(conf->all_modules, env);
    }

    if(conf->name_to_version_map)
    {
        axutil_hash_index_t *hi = NULL;
        void *key = NULL;
        void *val = NULL;
        for(hi = axutil_hash_first(conf->name_to_version_map, env); hi; hi = axutil_hash_next(env,
            hi))
        {
            axis2_char_t *module_ver = NULL;
            axutil_hash_this(hi, &key, NULL, &val);
            module_ver = (axis2_char_t *)val;
            if(module_ver)
            {
                AXIS2_FREE(env->allocator, module_ver);
            }
            if(key)
            {
                AXIS2_FREE(env->allocator, key);
            }
        }
        axutil_hash_free(conf->name_to_version_map, env);
    }

    if(conf->engaged_module_list)
    {
        for(i = 0; i < axutil_array_list_size(conf->engaged_module_list, env); i++)
        {
            axutil_qname_t *module_desc_qname = NULL;
            module_desc_qname = (axutil_qname_t *)axutil_array_list_get(conf->engaged_module_list,
                env, i);
            if(module_desc_qname)
            {
                axutil_qname_free(module_desc_qname, env);
            }
        }
        axutil_array_list_free(conf->engaged_module_list, env);
    }

    if(conf->out_phases)
    {
        for(i = 0; i < axutil_array_list_size(conf->out_phases, env); i++)
        {
            axis2_phase_t *phase = NULL;
            phase = (axis2_phase_t *)axutil_array_list_get(conf->out_phases, env, i);
            if(phase)
            {
                axis2_phase_free(phase, env);
            }
        }
        axutil_array_list_free(conf->out_phases, env);
    }

    if(conf->in_fault_phases)
    {
        for(i = 0; i < axutil_array_list_size(conf->in_fault_phases, env); i++)
        {
            axis2_phase_t *phase = NULL;
            phase = (axis2_phase_t *)axutil_array_list_get(conf->in_fault_phases, env, i);
            if(phase)
            {
                axis2_phase_free(phase, env);
            }
        }
        axutil_array_list_free(conf->in_fault_phases, env);
    }

    if(conf->out_fault_phases)
    {
        for(i = 0; i < axutil_array_list_size(conf->out_fault_phases, env); i++)
        {
            axis2_phase_t *phase = NULL;
            phase = (axis2_phase_t *)axutil_array_list_get(conf->out_fault_phases, env, i);
            if(phase)
            {
                axis2_phase_free(phase, env);
            }
        }
        axutil_array_list_free(conf->out_fault_phases, env);
    }

    if(conf->in_phases_upto_and_including_post_dispatch)
    {
        for(i = 0; i < axutil_array_list_size(conf-> in_phases_upto_and_including_post_dispatch,
            env); i++)
        {
            axis2_phase_t *phase = NULL;
            phase = (axis2_phase_t *)axutil_array_list_get(
                conf-> in_phases_upto_and_including_post_dispatch, env, i);
            if(phase)
            {
                axis2_phase_free(phase, env);
            }
        }
        axutil_array_list_free(conf-> in_phases_upto_and_including_post_dispatch, env);
    }

    if(conf->all_svcs)
    {
        axutil_hash_free(conf->all_svcs, env);
    }

    if(conf->all_init_svcs)
    {
        axutil_hash_free(conf->all_init_svcs, env);
    }

    if(conf->msg_recvs)
    {
        axutil_hash_index_t *hi = NULL;
        void *val = NULL;
        for(hi = axutil_hash_first(conf->msg_recvs, env); hi; hi = axutil_hash_next(env, hi))
        {
            axis2_msg_recv_t *msg_recv = NULL;
            axutil_hash_this(hi, NULL, NULL, &val);
            msg_recv = (axis2_msg_recv_t *)val;
            if(msg_recv)
            {
                axis2_msg_recv_free(msg_recv, env);
                msg_recv = NULL;
            }
        }
        axutil_hash_free(conf->msg_recvs, env);
    }

    if(conf->faulty_svcs)
    {
        axutil_hash_free(conf->faulty_svcs, env);
    }

    if(conf->faulty_modules)
    {
        axutil_hash_index_t *hi = NULL;
        void *val = NULL;
        for(hi = axutil_hash_first(conf->faulty_modules, env); hi; hi = axutil_hash_next(env, hi))
        {
            axis2_module_desc_t *module_desc = NULL;
            axutil_hash_this(hi, NULL, NULL, &val);
            module_desc = (axis2_module_desc_t *)val;
            if(module_desc)
            {
                axis2_module_desc_free(module_desc, env);
            }
        }
        axutil_hash_free(conf->faulty_modules, env);
    }

    if(conf->handlers)
    {
        int i = 0;
        for(i = 0; i < axutil_array_list_size(conf->handlers, env); i++)
        {
            axis2_handler_desc_t *handler_desc = NULL;
            handler_desc = (axis2_handler_desc_t *)axutil_array_list_get(conf->handlers, env, i);
            if(handler_desc)
            {
                axis2_handler_desc_free(handler_desc, env);
            }
        }
        axutil_array_list_free(conf->handlers, env);
    }

    if(conf->axis2_repo)
    {
        AXIS2_FREE(env->allocator, conf->axis2_repo);
    }

    if(conf->base)
    {
        axis2_desc_free(conf->base, env);
    }

    if(conf->axis2_xml)
    {
        AXIS2_FREE(env->allocator, conf->axis2_xml);
    }

    AXIS2_FREE(env->allocator, conf);
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_add_svc_grp(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_svc_grp_t * svc_grp)
{
    axutil_hash_t *svcs = NULL;
    axutil_hash_index_t *index_i = NULL;
    axis2_char_t *svc_name = NULL;
    const axis2_char_t *svc_grp_name = NULL;

    AXIS2_PARAM_CHECK(env->error, svc_grp, AXIS2_FAILURE);

    svcs = axis2_svc_grp_get_all_svcs(svc_grp, env);
    if(!conf->all_svcs)
    {
        conf->all_svcs = axutil_hash_make(env);
        if(!conf->all_svcs)
        {
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating all services map failed");
            return AXIS2_FAILURE;
        }
    }

    index_i = axutil_hash_first(svcs, env);
    while(index_i)
    {
        void *value = NULL;
        axis2_svc_t *desc = NULL;
        axis2_svc_t *temp_svc = NULL;
        const axutil_qname_t *svc_qname = NULL;

        axutil_hash_this(index_i, NULL, NULL, &value);
        desc = (axis2_svc_t *)value;
        svc_qname = axis2_svc_get_qname(desc, env);
        svc_name = axutil_qname_get_localpart(svc_qname, env);
        temp_svc = axutil_hash_get(conf->all_svcs, svc_name, AXIS2_HASH_KEY_STRING);

        /* No two service names deployed in the engine can be same */
        if(temp_svc)
        {
            AXIS2_ERROR_SET(env->error, AXIS2_ERROR_TWO_SVCS_CANNOT_HAVE_SAME_NAME, AXIS2_FAILURE);
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "There is already a service called %s in the "
                "all services list of axis2 configuration.", svc_name);
            return AXIS2_FAILURE;
        }

        index_i = axutil_hash_next(env, index_i);
    }

    svcs = axis2_svc_grp_get_all_svcs(svc_grp, env);
    index_i = axutil_hash_first(svcs, env);

    while(index_i)
    {
        void *value = NULL;
        axis2_svc_t *desc = NULL;

        axutil_hash_this(index_i, NULL, NULL, &value);
        desc = (axis2_svc_t *)value;
        svc_name = axutil_qname_get_localpart(axis2_svc_get_qname(desc, env), env);
        axutil_hash_set(conf->all_svcs, svc_name, AXIS2_HASH_KEY_STRING, desc);
        index_i = axutil_hash_next(env, index_i);
    }

    svc_grp_name = axis2_svc_grp_get_name(svc_grp, env);
    if(!conf->svc_grps)
    {
        conf->svc_grps = axutil_hash_make(env);
        if(!conf->svc_grps)
        {
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating service group map failed");
            return AXIS2_FAILURE;
        }
    }

    axutil_hash_set(conf->svc_grps, svc_grp_name, AXIS2_HASH_KEY_STRING, svc_grp);

    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_svc_grp_t *AXIS2_CALL
axis2_conf_get_svc_grp(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * svc_grp_name)
{
    AXIS2_PARAM_CHECK(env->error, svc_grp_name, NULL);

    if(!conf->svc_grps)
    {
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_INVALID_STATE_CONF, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
            "Axis2 configuration does not contain a service group map");
        return NULL;
    }
    return (axis2_svc_grp_t *)(axutil_hash_get(conf->svc_grps, svc_grp_name, AXIS2_HASH_KEY_STRING));
}

AXIS2_EXTERN axutil_hash_t *AXIS2_CALL
axis2_conf_get_all_svc_grps(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->svc_grps;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_add_svc(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_svc_t * svc)
{
    axis2_phase_resolver_t *phase_resolver = NULL;
    axis2_svc_grp_t *svc_grp = NULL;
    const axis2_char_t *svc_grp_name = NULL;
    axis2_status_t status = AXIS2_FAILURE;

    AXIS2_PARAM_CHECK(env->error, svc, AXIS2_FAILURE);

    /* We need to first create a service group with the same name as the 
     * service and make it the parent of service */
    svc_grp_name = axis2_svc_get_name(svc, env);
    if(!svc_grp_name)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Service has no name set");
        return AXIS2_FAILURE;
    }

    svc_grp = axis2_svc_grp_create(env);
    if(!svc_grp)
    {
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_NO_MEMORY, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
            "Creating service group as parent of service %s failed", svc_grp_name);
        return AXIS2_FAILURE;
    }

    status = axis2_svc_grp_set_name(svc_grp, env, svc_grp_name);
    if(AXIS2_SUCCESS != status)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Setting name to service group failed");
        return status;
    }

    status = axis2_svc_grp_set_parent(svc_grp, env, conf);
    if(AXIS2_SUCCESS != status)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Setting parent to service group %s failed",
            svc_grp_name);
        return status;
    }

    phase_resolver = axis2_phase_resolver_create_with_config_and_svc(env, conf, svc);
    if(!phase_resolver)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating phase resolver failed for service %s",
            axis2_svc_get_name(svc, env));
        return AXIS2_FAILURE;
    }

    status = axis2_phase_resolver_build_execution_chains_for_svc(phase_resolver, env);
    axis2_phase_resolver_free(phase_resolver, env);
    if(AXIS2_SUCCESS != status)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Building chains failed within phase resolver "
            "for service %s", axis2_svc_get_name(svc, env));
        return status;
    }

    status = axis2_svc_grp_add_svc(svc_grp, env, svc);
    if(AXIS2_SUCCESS != status)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Adding service %s to service group %s failed",
            svc_grp_name, svc_grp_name);
        return status;
    }

    status = axis2_conf_add_svc_grp(conf, env, svc_grp);
    return status;
}

AXIS2_EXTERN axis2_svc_t *AXIS2_CALL
axis2_conf_get_svc(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * svc_name)
{
    AXIS2_PARAM_CHECK(env->error, svc_name, NULL);

    return axutil_hash_get(conf->all_svcs, svc_name, AXIS2_HASH_KEY_STRING);
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_remove_svc(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * svc_name)
{
    AXIS2_PARAM_CHECK(env->error, svc_name, AXIS2_FAILURE);

    axutil_hash_set(conf->all_svcs, svc_name, AXIS2_HASH_KEY_STRING, NULL);
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_add_param(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axutil_param_t * param)
{
    axis2_status_t status = AXIS2_FAILURE;
    axis2_char_t *param_name = axutil_param_get_name(param, env);

    AXIS2_PARAM_CHECK(env->error, param, AXIS2_FAILURE);

    if(axis2_conf_is_param_locked(conf, env, param_name))
    {
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_PARAMETER_LOCKED_CANNOT_OVERRIDE, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Parameter %s is locked for Axis2 configuration",
            param_name);
        return AXIS2_FAILURE;
    }
    else
    {
        status = axutil_param_container_add_param(conf->param_container, env, param);
    }
    return status;
}

AXIS2_EXTERN axutil_param_t *AXIS2_CALL
axis2_conf_get_param(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * name)
{
    AXIS2_PARAM_CHECK(env->error, name, NULL);

    if(!conf->param_container)
    {
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_INVALID_STATE_PARAM_CONTAINER, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Param container is not set in axis2 configuraion");
        return NULL;
    }

    return axutil_param_container_get_param(conf->param_container, env, name);

}

AXIS2_EXTERN axutil_array_list_t *AXIS2_CALL
axis2_conf_get_all_params(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return axutil_param_container_get_params(conf->param_container, env);

}

AXIS2_EXTERN axis2_bool_t AXIS2_CALL
axis2_conf_is_param_locked(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * param_name)
{
    axutil_param_t *param = NULL;

    AXIS2_PARAM_CHECK(env->error, param_name, AXIS2_FALSE);

    param = axis2_conf_get_param(conf, env, param_name);
    return (param && axutil_param_is_locked(param, env));
}

AXIS2_EXTERN axis2_transport_in_desc_t *AXIS2_CALL
axis2_conf_get_transport_in(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const AXIS2_TRANSPORT_ENUMS trans_enum)
{
    return (axis2_transport_in_desc_t *)conf->transports_in[trans_enum];
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_add_transport_in(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_transport_in_desc_t * transport,
    const AXIS2_TRANSPORT_ENUMS trans_enum)
{
    AXIS2_PARAM_CHECK(env->error, transport, AXIS2_FAILURE);

    conf->transports_in[trans_enum] = transport;

    return AXIS2_SUCCESS;

}

AXIS2_EXTERN axis2_transport_out_desc_t *AXIS2_CALL
axis2_conf_get_transport_out(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const AXIS2_TRANSPORT_ENUMS trans_enum)
{
    return conf->transports_out[trans_enum];
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_add_transport_out(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_transport_out_desc_t * transport,
    const AXIS2_TRANSPORT_ENUMS trans_enum)
{
    AXIS2_PARAM_CHECK(env->error, transport, AXIS2_FAILURE);

    conf->transports_out[trans_enum] = transport;

    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_transport_in_desc_t **AXIS2_CALL
axis2_conf_get_all_in_transports(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return (axis2_transport_in_desc_t **)conf->transports_in;
}

AXIS2_EXTERN axis2_module_desc_t *AXIS2_CALL
axis2_conf_get_module(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const axutil_qname_t * qname)
{
    axis2_char_t *name = NULL;
    axis2_module_desc_t *ret = NULL;
    axis2_char_t *module_name = NULL;
    axutil_qname_t *mod_qname = NULL;
    const axis2_char_t *def_mod_ver = NULL;

    AXIS2_PARAM_CHECK(env->error, qname, NULL);

    name = axutil_qname_to_string((axutil_qname_t *)qname, env);
    ret = (axis2_module_desc_t *)axutil_hash_get(conf->all_modules, name, AXIS2_HASH_KEY_STRING);
    if(ret)
    {
        return ret;
    }
    module_name = axutil_qname_get_localpart(qname, env);
    if(!module_name)
    {
        return NULL;
    }
    def_mod_ver = axis2_conf_get_default_module_version(conf, env, module_name);
    mod_qname = axis2_core_utils_get_module_qname(env, name, def_mod_ver);
    if(!mod_qname)
    {
        return NULL;
    }
    name = axutil_qname_to_string(mod_qname, env);
    ret = (axis2_module_desc_t *)axutil_hash_get(conf->all_modules, name, AXIS2_HASH_KEY_STRING);
    axutil_qname_free(mod_qname, env);
    mod_qname = NULL;
    return ret;
}

AXIS2_EXTERN axutil_array_list_t *AXIS2_CALL
axis2_conf_get_all_engaged_modules(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->engaged_module_list;
}

AXIS2_EXTERN axutil_array_list_t *AXIS2_CALL
axis2_conf_get_in_phases_upto_and_including_post_dispatch(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->in_phases_upto_and_including_post_dispatch;
}

AXIS2_EXTERN axutil_array_list_t *AXIS2_CALL
axis2_conf_get_out_flow(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->out_phases;
}

AXIS2_EXTERN axutil_array_list_t *AXIS2_CALL
axis2_conf_get_in_fault_flow(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->in_fault_phases;
}

AXIS2_EXTERN axutil_array_list_t *AXIS2_CALL
axis2_conf_get_out_fault_flow(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->out_fault_phases;
}

AXIS2_EXTERN axis2_transport_out_desc_t **AXIS2_CALL
axis2_conf_get_all_out_transports(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return (axis2_transport_out_desc_t **)conf->transports_out;
}

AXIS2_EXTERN axutil_hash_t *AXIS2_CALL
axis2_conf_get_all_faulty_svcs(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->faulty_svcs;
}

AXIS2_EXTERN axutil_hash_t *AXIS2_CALL
axis2_conf_get_all_faulty_modules(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->faulty_modules;
}

AXIS2_EXTERN axutil_hash_t *AXIS2_CALL
axis2_conf_get_all_svcs(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    /*axutil_hash_t *sgs = NULL;
     axutil_hash_index_t *index_i = NULL;
     axutil_hash_index_t *index_j = NULL;
     void *value = NULL;
     void *value2 = NULL;
     axis2_svc_grp_t *axis_svc_grp = NULL;
     axutil_hash_t *svcs = NULL;
     axis2_svc_t *svc = NULL;
     axis2_char_t *svc_name = NULL;
     */

    /* Do we need to do all the following of retrieving all service groups and
     * then add all services from each group to conf->all_svcs and then finally 
     * return conf->all_svcs?. We have already done this when
     * adding each service group to the conf, so just returning conf->all_svcs
     * here would be enough - Damitha */
    /*sgs = axis2_conf_get_all_svc_grps(conf, env);
     index_i = axutil_hash_first(sgs, env);
     while(index_i)
     {
     axutil_hash_this(index_i, NULL, NULL, &value);
     axis_svc_grp = (axis2_svc_grp_t *)value;
     svcs = axis2_svc_grp_get_all_svcs(axis_svc_grp, env);
     index_j = axutil_hash_first(svcs, env);
     while(index_j)
     {
     axutil_hash_this(index_j, NULL, NULL, &value2);
     svc = (axis2_svc_t *)value2;
     svc_name = axutil_qname_get_localpart(axis2_svc_get_qname(svc, env), env);
     axutil_hash_set(conf->all_svcs, svc_name, AXIS2_HASH_KEY_STRING, svc);

     index_j = axutil_hash_next(env, index_j);
     }

     index_i = axutil_hash_next(env, index_i);
     }*/
    return conf->all_svcs;
}

AXIS2_EXTERN axutil_hash_t *AXIS2_CALL
axis2_conf_get_all_svcs_to_load(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    axutil_hash_t *sgs = NULL;
    axutil_hash_index_t *index_i = NULL;
    axutil_hash_index_t *index_j = NULL;
    void *value = NULL;
    void *value2 = NULL;
    axis2_svc_grp_t *axis_svc_grp = NULL;
    axutil_hash_t *svcs = NULL;
    axis2_svc_t *svc = NULL;
    axis2_char_t *svc_name = NULL;

    sgs = axis2_conf_get_all_svc_grps(conf, env);
    index_i = axutil_hash_first(sgs, env);
    while(index_i)
    {
        axutil_hash_this(index_i, NULL, NULL, &value);
        axis_svc_grp = (axis2_svc_grp_t *)value;
        svcs = axis2_svc_grp_get_all_svcs(axis_svc_grp, env);
        index_j = axutil_hash_first(svcs, env);
        while(index_j)
        {
            axutil_param_t *param = NULL;
            axutil_hash_this(index_j, NULL, NULL, &value2);
            svc = (axis2_svc_t *)value2;
            svc_name = axutil_qname_get_localpart(axis2_svc_get_qname(svc, env), env);
            param = axis2_svc_get_param(svc, env, AXIS2_LOAD_SVC_STARTUP);
            if(param)
            {
                axutil_hash_set(conf->all_init_svcs, svc_name, AXIS2_HASH_KEY_STRING, svc);
            }

            index_j = axutil_hash_next(env, index_j);
        }

        index_i = axutil_hash_next(env, index_i);
    }
    return conf->all_init_svcs;
}

AXIS2_EXTERN axis2_bool_t AXIS2_CALL
axis2_conf_is_engaged(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    const axutil_qname_t * module_name)
{
    const axutil_qname_t *def_mod_qname = NULL;
    axis2_module_desc_t *def_mod = NULL;
    int i = 0;
    int size = 0;

    AXIS2_PARAM_CHECK(env->error, module_name, AXIS2_FALSE);

    def_mod
        = axis2_conf_get_default_module(conf, env, axutil_qname_get_localpart(module_name, env));
    if(def_mod)
    {
        def_mod_qname = axis2_module_desc_get_qname(def_mod, env);
    }

    size = axutil_array_list_size(conf->engaged_module_list, env);
    for(i = 0; i < size; i++)
    {
        axutil_qname_t *qname = NULL;
        qname = (axutil_qname_t *)axutil_array_list_get(conf->engaged_module_list, env, i);
        if(axutil_qname_equals(module_name, env, qname) || (def_mod_qname && axutil_qname_equals(
            def_mod_qname, env, qname)))
        {
            return AXIS2_TRUE;
        }
    }

    return AXIS2_FALSE;
}

AXIS2_EXTERN axis2_phases_info_t *AXIS2_CALL
axis2_conf_get_phases_info(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->phases_info;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_phases_info(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_phases_info_t * phases_info)
{
    AXIS2_PARAM_CHECK(env->error, phases_info, AXIS2_FAILURE);

    if(conf->phases_info)
    {
        axis2_phases_info_free(phases_info, env);
        conf->phases_info = NULL;
    }
    conf->phases_info = phases_info;
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_add_msg_recv(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * key,
    axis2_msg_recv_t * msg_recv)
{
    AXIS2_PARAM_CHECK(env->error, key, AXIS2_FAILURE);
    AXIS2_PARAM_CHECK(env->error, msg_recv, AXIS2_FAILURE);
    if(!conf->msg_recvs)
    {
        conf->msg_recvs = axutil_hash_make(env);
        if(!conf->msg_recvs)
        {
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating message receiver map failed");
            return AXIS2_FAILURE;
        }
    }
    axutil_hash_set(conf->msg_recvs, key, AXIS2_HASH_KEY_STRING, msg_recv);
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_msg_recv_t *AXIS2_CALL
axis2_conf_get_msg_recv(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_char_t * key)
{
    return (axis2_msg_recv_t *)axutil_hash_get(conf->msg_recvs, key, AXIS2_HASH_KEY_STRING);
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_out_phases(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axutil_array_list_t * out_phases)
{
    AXIS2_PARAM_CHECK(env->error, out_phases, AXIS2_FAILURE);

    if(conf->out_phases)
    {
        axutil_array_list_free(conf->out_phases, env);
        conf->out_phases = NULL;
    }
    conf->out_phases = out_phases;
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axutil_array_list_t *AXIS2_CALL
axis2_conf_get_out_phases(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->out_phases;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_in_fault_phases(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axutil_array_list_t * list)
{
    AXIS2_PARAM_CHECK(env->error, list, AXIS2_FAILURE);

    if(conf->in_fault_phases)
    {
        axutil_array_list_free(conf->in_fault_phases, env);
        conf->in_fault_phases = NULL;
    }
    conf->in_fault_phases = list;
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_out_fault_phases(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axutil_array_list_t * list)
{
    AXIS2_PARAM_CHECK(env->error, list, AXIS2_FAILURE);

    if(conf->out_fault_phases)
    {
        axutil_array_list_free(conf->out_fault_phases, env);
        conf->out_fault_phases = NULL;
    }
    conf->out_fault_phases = list;
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axutil_hash_t *AXIS2_CALL
axis2_conf_get_all_modules(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->all_modules;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_add_module(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_module_desc_t * module)
{
    const axutil_qname_t *module_qname = NULL;

    AXIS2_PARAM_CHECK(env->error, module, AXIS2_FAILURE);

    axis2_module_desc_set_parent(module, env, conf);

    module_qname = axis2_module_desc_get_qname(module, env);
    if(module_qname)
    {
        axis2_char_t *module_name = NULL;
        module_name = axutil_qname_to_string((axutil_qname_t *)module_qname, env);
        axutil_hash_set(conf->all_modules, module_name, AXIS2_HASH_KEY_STRING, module);
    }

    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_default_dispatchers(
    axis2_conf_t * conf,
    const axutil_env_t * env)
{
    axis2_phase_t *dispatch = NULL;
    axis2_status_t status = AXIS2_FAILURE;
    axis2_disp_t *rest_dispatch = NULL;
    axis2_disp_t *soap_action_based_dispatch = NULL;
    axis2_disp_t *soap_msg_body_based_dispatch = NULL;
    axis2_handler_t *handler = NULL;
    axis2_phase_t *post_dispatch = NULL;
    axis2_disp_checker_t *disp_checker = NULL;

    dispatch = axis2_phase_create(env, AXIS2_PHASE_DISPATCH);
    if(!dispatch)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating phase %s failed", AXIS2_PHASE_DISPATCH);
        return AXIS2_FAILURE;
    }

    rest_dispatch = axis2_rest_disp_create(env);
    if(!rest_dispatch)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating rest dispatcher failed");
        return AXIS2_FAILURE;
    }

    handler = axis2_disp_get_base(rest_dispatch, env);
    axis2_disp_free(rest_dispatch, env);
    axis2_phase_add_handler_at(dispatch, env, 0, handler);
    axutil_array_list_add(conf->handlers, env, axis2_handler_get_handler_desc(handler, env));

    soap_msg_body_based_dispatch = axis2_soap_body_disp_create(env);
    if(!soap_msg_body_based_dispatch)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating soap body based dispatcher failed");
        return AXIS2_FAILURE;
    }

    handler = axis2_disp_get_base(soap_msg_body_based_dispatch, env);
    axis2_disp_free(soap_msg_body_based_dispatch, env);
    axis2_phase_add_handler_at(dispatch, env, 1, handler);
    axutil_array_list_add(conf->handlers, env, axis2_handler_get_handler_desc(handler, env));

    soap_action_based_dispatch = axis2_soap_action_disp_create(env);
    if(!soap_action_based_dispatch)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating soap action based dispatcher failed");
        return AXIS2_FAILURE;
    }

    handler = axis2_disp_get_base(soap_action_based_dispatch, env);
    axis2_disp_free(soap_action_based_dispatch, env);
    axis2_phase_add_handler_at(dispatch, env, 2, handler);
    axutil_array_list_add(conf->handlers, env, axis2_handler_get_handler_desc(handler, env));

    status
        = axutil_array_list_add(conf-> in_phases_upto_and_including_post_dispatch, env, dispatch);
    if(AXIS2_SUCCESS != status)
    {
        axis2_phase_free(dispatch, env);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
            "Adding dispatcher into in phases upto and including post dispatch list failed");
        return status;
    }

    post_dispatch = axis2_phase_create(env, AXIS2_PHASE_POST_DISPATCH);
    if(!post_dispatch)
    {
        axis2_phase_free(dispatch, env);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating phase %s failed",
            AXIS2_PHASE_POST_DISPATCH);
        return AXIS2_FAILURE;
    }

    disp_checker = axis2_disp_checker_create(env);
    handler = axis2_disp_checker_get_base(disp_checker, env);
    axis2_disp_checker_free(disp_checker, env);
    axis2_phase_add_handler_at(post_dispatch, env, 0, handler);
    axutil_array_list_add(conf->handlers, env, axis2_handler_get_handler_desc(handler, env));
    handler = axis2_ctx_handler_create(env, NULL);
    axis2_phase_add_handler_at(post_dispatch, env, 1, handler);
    axutil_array_list_add(conf->handlers, env, axis2_handler_get_handler_desc(handler, env));

    status = axutil_array_list_add(conf-> in_phases_upto_and_including_post_dispatch, env,
        post_dispatch);
    if(AXIS2_SUCCESS != status)
    {
        axis2_phase_free(dispatch, env);
        axis2_phase_free(post_dispatch, env);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
            "Adding post dispatcher into in phases upto and including post dispatch list failed");
        return status;
    }
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_dispatch_phase(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_phase_t * dispatch)
{
    axis2_status_t status = AXIS2_FAILURE;
    axis2_handler_t *handler = NULL;
    axis2_phase_t *post_dispatch = NULL;
    axis2_disp_checker_t *disp_checker = NULL;

    AXIS2_PARAM_CHECK(env->error, dispatch, AXIS2_FAILURE);

    status
        = axutil_array_list_add(conf-> in_phases_upto_and_including_post_dispatch, env, dispatch);
    if(AXIS2_FAILURE == status)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
            "Adding dispatcher into in phases upto and including post dispatch list failed");
        return AXIS2_FAILURE;
    }

    post_dispatch = axis2_phase_create(env, AXIS2_PHASE_POST_DISPATCH);
    if(!post_dispatch)
    {
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Creating phase %s failed",
            AXIS2_PHASE_POST_DISPATCH);
        axis2_phase_free(dispatch, env);
        return AXIS2_FAILURE;
    }

    disp_checker = axis2_disp_checker_create(env);

    handler = axis2_disp_checker_get_base(disp_checker, env);
    axis2_phase_add_handler_at(post_dispatch, env, 0, handler);

    status = axutil_array_list_add(conf-> in_phases_upto_and_including_post_dispatch, env,
        post_dispatch);
    if(AXIS2_FAILURE == status)
    {
        axis2_phase_free(dispatch, env);
        axis2_phase_free(post_dispatch, env);
        axis2_disp_checker_free(disp_checker, env);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
            "Adding post dispatcher into in phases upto and including post dispatch list failed");
        return AXIS2_FAILURE;
    }
    return AXIS2_SUCCESS;
}

/**
 * For each module reference qname stored in dep_engine this function is called.
 * All module_desc instances are stored in axis2_conf. So each module_desc
 * is retrieved from there by giving module_qname and engaged globally by
 * calling phase_resolvers engage_module_globally function. Modules are added
 * to axis2_conf's engaged module list.
 */
AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_engage_module(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    const axutil_qname_t * module_ref)
{
    axis2_module_desc_t *module_desc = NULL;
    axis2_bool_t is_new_module = AXIS2_FALSE;
    axis2_bool_t to_be_engaged = AXIS2_TRUE;
    axis2_dep_engine_t *dep_engine = NULL;
    axis2_status_t status = AXIS2_FAILURE;
    axis2_char_t *file_name = NULL;

    AXIS2_PARAM_CHECK(env->error, module_ref, AXIS2_FAILURE);
    AXIS2_PARAM_CHECK(env->error, conf, AXIS2_FAILURE);

    module_desc = axis2_conf_get_module(conf, env, module_ref);
    if(!module_desc)
    {
        axutil_file_t *file = NULL;
        const axis2_char_t *repos_path = NULL;
        axis2_arch_file_data_t *file_data = NULL;
        axis2_char_t *temp_path1 = NULL;
        axis2_char_t *temp_path2 = NULL;
        axis2_char_t *temp_path3 = NULL;
        axis2_char_t *path = NULL;
        axutil_param_t *module_dir_param = NULL;
        axis2_char_t *module_dir = NULL;
        axis2_bool_t flag;
        /*axis2_char_t *axis2_xml = NULL;*/

        file_name = axutil_qname_get_localpart(module_ref, env);
        file = (axutil_file_t *)axis2_arch_reader_create_module_arch(env, file_name);
        /* This flag is to check whether conf is built using axis2
         * xml configuration file instead of a repository. */
        flag = axis2_conf_get_axis2_flag(conf, env);

        if(!flag)
        {
            repos_path = axis2_conf_get_repo(conf, env);
            temp_path1 = axutil_stracat(env, repos_path, AXIS2_PATH_SEP_STR);
            temp_path2 = axutil_stracat(env, temp_path1, AXIS2_MODULE_FOLDER);
            temp_path3 = axutil_stracat(env, temp_path2, AXIS2_PATH_SEP_STR);
            path = axutil_stracat(env, temp_path3, file_name);
            AXIS2_FREE(env->allocator, temp_path1);
            AXIS2_FREE(env->allocator, temp_path2);
            AXIS2_FREE(env->allocator, temp_path3);
        }
        else
        {
            /**
             * This case is to obtain module path from the axis2.xml
             */
            /* axis2_xml = (axis2_char_t *)axis2_conf_get_axis2_xml(conf, env);*/
            module_dir_param = axis2_conf_get_param(conf, env, AXIS2_MODULE_DIR);

            if(module_dir_param)
            {
                module_dir = (axis2_char_t *)axutil_param_get_value(module_dir_param, env);
            }
            else
            {
                AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI,
                    "moduleDir parameter not available in axis2.xml.");

                return AXIS2_FAILURE;
            }

            temp_path1 = axutil_strcat(env, module_dir, AXIS2_PATH_SEP_STR, NULL);
            path = axutil_strcat(env, temp_path1, file_name, NULL);
        }

        axutil_file_set_path(file, env, path);
        file_data = axis2_arch_file_data_create_with_type_and_file(env, AXIS2_MODULE, file);
        /*if(!flag)
        {
            dep_engine = axis2_dep_engine_create_with_repos_name(env, repos_path);
        }
        else
        {
            dep_engine = axis2_dep_engine_create_with_axis2_xml(env, axis2_xml);
        }*/
        dep_engine = conf->dep_engine;

        axis2_dep_engine_set_current_file_item(dep_engine, env, file_data);

        /* this module_dir set the path of the module directory
         * Pertaining to this module. This value will use inside the
         * axis2_dep_engine_build_module function
         */

        axis2_dep_engine_set_module_dir(dep_engine, env, path);

        if(path)
        {
            AXIS2_FREE(env->allocator, path);
        }

        if(file_data)
        {
            axis2_arch_file_data_free(file_data, env);
        }

        module_desc = axis2_dep_engine_build_module(dep_engine, env, file, conf);
        axutil_file_free(file, env);
        is_new_module = AXIS2_TRUE;
    }

    if(module_desc)
    {
        int size = 0;
        int i = 0;
        const axutil_qname_t *module_qname = NULL;

        size = axutil_array_list_size(conf->engaged_module_list, env);
        module_qname = axis2_module_desc_get_qname(module_desc, env);
        for(i = 0; i < size; i++)
        {
            axutil_qname_t *qname = NULL;

            qname = (axutil_qname_t *)axutil_array_list_get(conf->engaged_module_list, env, i);
            if(axutil_qname_equals(module_qname, env, qname))
            {
                to_be_engaged = AXIS2_FALSE;
            }
        }
    }
    else
    {
        AXIS2_ERROR_SET(env->error, AXIS2_ERROR_INVALID_MODULE, AXIS2_FAILURE);
        AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Either module description not set or building"
            "module description failed for module %s", file_name);

        return AXIS2_FAILURE;
    }

    if(to_be_engaged)
    {
        axis2_phase_resolver_t *phase_resolver = NULL;
        axutil_qname_t *module_qref_l = NULL;
        const axutil_qname_t *module_qname = NULL;
        axis2_char_t *module_name = NULL;

        module_qname = axis2_module_desc_get_qname(module_desc, env);
        module_name = axutil_qname_get_localpart(module_qname, env);
        phase_resolver = axis2_phase_resolver_create_with_config(env, conf);
        if(!phase_resolver)
        {
            return AXIS2_FAILURE;
        }

        status = axis2_phase_resolver_engage_module_globally(phase_resolver, env, module_desc);
        axis2_phase_resolver_free(phase_resolver, env);
        if(!status)
        {
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "Engaging module %s globally failed",
                module_name);
            return status;
        }
        module_qref_l = axutil_qname_clone((axutil_qname_t *)module_qname, env);
        status = axutil_array_list_add(conf->engaged_module_list, env, module_qref_l);
    }

    if(is_new_module)
    {
        status = axis2_conf_add_module(conf, env, module_desc);
    }

    return status;
}

AXIS2_EXTERN const axis2_char_t *AXIS2_CALL
axis2_conf_get_repo(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->axis2_repo;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_repo(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_char_t * repos_path)
{
    if(conf->axis2_repo)
    {
        AXIS2_FREE(env->allocator, conf->axis2_repo);
        conf->axis2_repo = NULL;
    }
    conf->axis2_repo = axutil_strdup(env, repos_path);
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN const axis2_char_t *AXIS2_CALL
axis2_conf_get_axis2_xml(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return axutil_strdup(env, conf->axis2_xml);
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_axis2_xml(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_char_t * axis2_xml)
{
    AXIS2_PARAM_CHECK(env->error, axis2_xml, AXIS2_FAILURE);
    conf->axis2_xml = axutil_strdup(env, axis2_xml);
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN struct axis2_dep_engine * AXIS2_CALL
axis2_conf_get_dep_engine(
    axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->dep_engine;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_dep_engine(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_dep_engine_t * dep_engine)
{
    conf->dep_engine = dep_engine;
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN const axis2_char_t *AXIS2_CALL
axis2_conf_get_default_module_version(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * module_name)
{
    axutil_hash_t *def_ver_map = NULL;
    AXIS2_PARAM_CHECK(env->error, module_name, NULL);

    def_ver_map = conf->name_to_version_map;
    if(!def_ver_map)
    {
        return NULL;
    }
    return axutil_hash_get(def_ver_map, module_name, AXIS2_HASH_KEY_STRING);
}

AXIS2_EXTERN axis2_module_desc_t *AXIS2_CALL
axis2_conf_get_default_module(
    const axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * module_name)
{
    axis2_module_desc_t *ret_mod = NULL;
    axis2_char_t *mod_name = NULL;
    const axis2_char_t *mod_ver = NULL;
    axutil_hash_t *all_modules = NULL;
    axutil_qname_t *mod_qname = NULL;

    AXIS2_PARAM_CHECK(env->error, module_name, NULL);

    all_modules = conf->all_modules;
    mod_ver = axis2_conf_get_default_module_version(conf, env, module_name);

    if(!mod_ver)
    {
        mod_name = axutil_strdup(env, module_name);
    }
    else
    {
        axis2_char_t *tmp_name = NULL;
        tmp_name = axutil_stracat(env, module_name, "-");
        mod_name = axutil_stracat(env, tmp_name, mod_ver);
        AXIS2_FREE(env->allocator, tmp_name);
    }
    mod_qname = axutil_qname_create(env, mod_name, NULL, NULL);
    AXIS2_FREE(env->allocator, mod_name);
    mod_name = NULL;

    if(!mod_qname)
    {
        return NULL;
    }
    ret_mod = (axis2_module_desc_t *)axutil_hash_get(all_modules, axutil_qname_to_string(mod_qname,
        env), AXIS2_HASH_KEY_STRING);

    axutil_qname_free(mod_qname, env);

    return ret_mod;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_add_default_module_version(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * module_name,
    const axis2_char_t * module_version)
{
    axutil_hash_t *name_to_ver_map = NULL;

    AXIS2_PARAM_CHECK(env->error, module_name, AXIS2_FAILURE);
    AXIS2_PARAM_CHECK(env->error, module_version, AXIS2_FAILURE);
    /*
     * If we already have a default module version we don't put
     * it again
     */
    name_to_ver_map = conf->name_to_version_map;

    if(!axutil_hash_get(name_to_ver_map, module_name, AXIS2_HASH_KEY_STRING))
    {
        axis2_char_t *new_entry = axutil_strdup(env, module_version);
        if(!new_entry)
        {
            return AXIS2_FAILURE;
        }
        axis2_char_t *new_name = axutil_strdup(env, module_name);
        if(!new_entry)
        {
            AXIS2_FREE(env->allocator, new_entry);
            return AXIS2_FAILURE;
        }

        axutil_hash_set(name_to_ver_map, new_name, AXIS2_HASH_KEY_STRING, new_entry);
        return AXIS2_SUCCESS;
    }
    return AXIS2_FAILURE;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_engage_module_with_version(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    const axis2_char_t * module_name,
    const axis2_char_t * version_id)
{
    axutil_qname_t *module_qname = NULL;
    axis2_status_t status = AXIS2_FAILURE;

    AXIS2_PARAM_CHECK(env->error, module_name, AXIS2_FAILURE);

    module_qname = axis2_core_utils_get_module_qname(env, module_name, version_id);
    if(!module_qname)
    {
        return AXIS2_FAILURE;
    }
    status = axis2_conf_engage_module(conf, env, module_qname);
    axutil_qname_free(module_qname, env);
    return status;
}

AXIS2_EXTERN axis2_bool_t AXIS2_CALL
axis2_conf_get_enable_mtom(
    axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->enable_mtom;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_enable_mtom(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_bool_t enable_mtom)
{
    conf->enable_mtom = enable_mtom;
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_bool_t AXIS2_CALL
axis2_conf_get_axis2_flag(
    axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->axis2_flag;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_axis2_flag(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_bool_t axis2_flag)
{
    conf->axis2_flag = axis2_flag;
    return AXIS2_SUCCESS;
}

AXIS2_EXTERN axis2_bool_t AXIS2_CALL
axis2_conf_get_enable_security(
    axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->enable_security;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_enable_security(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    axis2_bool_t enable_security)
{
    AXIS2_PARAM_CHECK(env->error, conf, AXIS2_FAILURE);

    conf->enable_security = enable_security;
    return AXIS2_SUCCESS;
}

#if 0
/* this seemed to be not used after 1.6.0 */
AXIS2_EXTERN void *AXIS2_CALL
axis2_conf_get_security_context(
    axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->security_context;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_set_security_context(
    axis2_conf_t * conf,
    const axutil_env_t * env,
    void *security_context)
{
    AXIS2_PARAM_CHECK(env->error, conf, AXIS2_FAILURE);

    conf->security_context = (void *) security_context;
    return AXIS2_SUCCESS;
}
#endif

AXIS2_EXTERN axutil_param_container_t *AXIS2_CALL
axis2_conf_get_param_container(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->param_container;
}

AXIS2_EXTERN axis2_desc_t *AXIS2_CALL
axis2_conf_get_base(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->base;
}

AXIS2_EXTERN axutil_array_list_t * AXIS2_CALL
axis2_conf_get_handlers(
    const axis2_conf_t * conf,
    const axutil_env_t * env)
{
    return conf->handlers;
}

AXIS2_EXTERN axis2_status_t AXIS2_CALL
axis2_conf_disengage_module(
	const axis2_conf_t *conf,
	const axutil_env_t *env,
	const axutil_qname_t *module_ref)
{
	axis2_module_desc_t *module_desc = NULL;
	axutil_hash_index_t *index = NULL;
	axis2_char_t *mod_name  =  NULL;
	int size = 0, i = 0;
	if(!module_ref)
		return AXIS2_FAILURE;

	mod_name = axutil_qname_get_localpart(module_ref, env);
    module_desc = axis2_conf_get_module(conf, env, module_ref);
    if(!module_desc)
    {
		AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "The requested module %s does not exist",
			mod_name);
		return AXIS2_FAILURE;
    }
	if(!axis2_conf_is_engaged((axis2_conf_t*)conf, env, module_ref))
	{
		AXIS2_LOG_INFO(env->log, AXIS2_LOG_SI, "%s Module is not engaged globally", mod_name);
		return AXIS2_FAILURE;
	}
	if(!conf->all_svcs)
	{
		AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, "configuration does not have any services");
		return AXIS2_FAILURE;
	
	}
	for(index = axutil_hash_first(conf->all_svcs, env); index; index = axutil_hash_next(env, index))
	{
		axis2_svc_t *svc = NULL;
		void *v = NULL;
		/*const axis2_char_t *svc_name = NULL; */
		axutil_hash_this(index, NULL, NULL, &v);
		svc = (axis2_svc_t *)v;
		if(svc)
		{
			/*svc_name = axis2_svc_get_name(svc, env); */
			axis2_svc_disengage_module(svc, env, module_desc,(axis2_conf_t*) conf);				
		}
	}
	
	size = axutil_array_list_size(conf->engaged_module_list, env);
    for(i = 0; i < size; i++)
    {
        axutil_qname_t *qname = NULL;
        qname = (axutil_qname_t *)axutil_array_list_get(conf->engaged_module_list, env, i);
		if(axutil_qname_equals(module_ref, env, qname))
        {
			axutil_array_list_remove(conf->engaged_module_list, env, i);
			return AXIS2_SUCCESS;
		}
    }

	return AXIS2_FAILURE;
}

