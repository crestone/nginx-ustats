
/**
 * Copyright (c) 2010-2011 Aleksey Fedotov
 * http://skbkontur.ru
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>




const char RESPONSE_START[] =
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\">";


const char RESPONSE_END[] =
        "</html>";


const char RESPONSE_HEAD[] =
        "<head>"
            "<meta http-equiv=\"Pragma\" content=\"no-cache\">"
            "<style type=\"text/css\">"
               ".statsTable"
               "{"
                   "border-collapse: collapse;"
                   "border-width: 1px;"
                   "border-style: outset;"
               "}"

               ".stcCommon"
               "{"
               "    border-width: 1px;"
               "    border-style: solid;"
               "    border-color: black;"
               "    padding: 4px;"
               "    text-align: center;"
               "}"

               ".stcHeader"
               "{"
               "    background-color: #12B2C4;"
               "}"

               ".stcBlacklisted"
               "{"
               "    background-color: red;"
               "}"

               ".stcDisabled"
               "{"
               "    border-color: black;"
               "    color: #A8A8A8;"
               "}"

			   ".stcNoLastFail"
			   "{"
			   "	color: #EEEEEE;"
			   "}"

			   ".stcImplicitUpstream"
			   "{"
			   "	color: #FFFFFF;"
			   "}"

               "/* Firefox seems to not like td:hover */"
               ".stcPeerHover"
               "{"
               "    text-decoration: underline;"
               "    color: blue;"
               "}"

            "</style>"

            "<script type=\"text/javascript\">"
                "window.onload = function()"
                "{"
				"	setTimeout('window.location.reload()', '%d');"
                "};"
            "</script>"

            "<title>NGINX upstream statistics</title>"
        "</head>";


const char RESPONSE_BODY_START[] =
        "<body>"
            "<table width=\"100%%\" height=\"100%%\">"
                "<tr><td align=\"center\">";


const char RESPONSE_BODY_END[] =
                "</td></tr>"
            "</table>"
        "</body>";



/**
 * Shared memory used to store all the stats
 */
ngx_shm_zone_t * stats_data = NULL;




typedef struct
{
    /**
     * Resulting table width and height.
     * Less or equal to 100 - percent value, greater than 100 - pixel value.
     * 70 by default.
     */
    ngx_uint_t html_table_width;
    ngx_uint_t html_table_height;
    /** Page refresh interval, milliseconds. 5000 by default */
    ngx_uint_t refresh_interval;
} ngx_http_ustats_loc_conf_t;




static void * ngx_http_ustats_create_loc_conf(ngx_conf_t *cf);
static char * ngx_http_ustats_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static char * ngx_http_set_ustats(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_buf_t * ngx_http_ustats_create_response_full_html(ngx_http_request_t * r);



static ngx_command_t  ngx_http_ustats_commands[] =
{
    { 
        ngx_string("ustats"),
        NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
        ngx_http_set_ustats,
        0,
        0,
        NULL 
    },

    {
        ngx_string("ustats_html_table_width"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_ustats_loc_conf_t, html_table_width),
        NULL
    },

    {
        ngx_string("ustats_html_table_height"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_ustats_loc_conf_t, html_table_height),
        NULL
    },

    {
        ngx_string("ustats_refresh_interval"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_num_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_ustats_loc_conf_t, refresh_interval),
        NULL
    },

    ngx_null_command
};



static ngx_http_module_t  ngx_http_ustats_module_ctx =
{
    NULL,                                  /* preconfiguration */
    NULL,					               /* postconfiguration */

    NULL,							       /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_ustats_create_loc_conf,       /* create location configuration */
    ngx_http_ustats_merge_loc_conf         /* merge location configuration */
};



ngx_module_t  ngx_http_ustats_module =
{
    NGX_MODULE_V1,
    &ngx_http_ustats_module_ctx,           /* module context */
    ngx_http_ustats_commands,              /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


/*****************************************************************************/
/*****************************************************************************/


static void * ngx_http_ustats_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_ustats_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ustats_loc_conf_t));
    if (conf == NULL)
        return NGX_CONF_ERROR;

    conf->html_table_width = NGX_CONF_UNSET_UINT;
    conf->html_table_height = NGX_CONF_UNSET_UINT;
    conf->refresh_interval = NGX_CONF_UNSET_UINT;

    return conf;
}


/*****************************************************************************/
/*****************************************************************************/


static char* ngx_http_ustats_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_ustats_loc_conf_t *prev = parent;
    ngx_http_ustats_loc_conf_t *conf = child;

    ngx_conf_merge_uint_value(conf->html_table_width, prev->html_table_width, 70);
    ngx_conf_merge_uint_value(conf->html_table_height, prev->html_table_height, 70);
    ngx_conf_merge_uint_value(conf->refresh_interval, prev->refresh_interval, 5000);

    if (conf->html_table_width < 1)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "html table width must be >= 1");
        return NGX_CONF_ERROR;
    }

    if (conf->html_table_height < 1)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "html table height must be >= 1");
        return NGX_CONF_ERROR;
    }

    if (conf->refresh_interval < 1)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "page refresh interval must be >= 1");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/*****************************************************************************/
/*****************************************************************************/


static ngx_int_t ngx_http_ustats_handler(ngx_http_request_t *r)
{
    ngx_int_t rc;
    ngx_buf_t *b = NULL;
    ngx_chain_t out;

    if (r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD)
        return NGX_HTTP_NOT_ALLOWED;

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK)
        return rc;

    ngx_str_set(&r->headers_out.content_type, "text/plain");

    if (r->method == NGX_HTTP_HEAD)
    {
        r->headers_out.status = NGX_HTTP_OK;

        rc = ngx_http_send_header(r);

        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only)
            return rc;
    }

	ngx_str_set(&r->headers_out.content_type, "text/html");
	b = ngx_http_ustats_create_response_full_html(r);

    if (b == NULL)
        return NGX_HTTP_INTERNAL_SERVER_ERROR;

    out.buf = b;
    out.next = NULL;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;

    b->last_buf = 1;

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only)
        return rc;

    return ngx_http_output_filter(r, &out);
}


/*****************************************************************************/
/*****************************************************************************/


static ngx_int_t ngx_http_ustats_init_stats_data_shm(ngx_shm_zone_t * shm_zone, void * data)
{
	if (data)
	{
		shm_zone->data = data;
		return NGX_OK;
	}

	void * new_data = ngx_slab_alloc((ngx_slab_pool_t*)shm_zone->shm.addr, 27 * ngx_pagesize);

	if (!new_data)
		return NGX_ERROR;

	shm_zone->data = new_data;

	return NGX_OK;
}


/*****************************************************************************/
/*****************************************************************************/


static char *ngx_http_set_ustats(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_ustats_handler;

    ngx_str_t * stats_data_shm_name = NULL;

    stats_data_shm_name = ngx_palloc(cf->pool, sizeof(*stats_data_shm_name));
    stats_data_shm_name->len = sizeof("stats_data");
    stats_data_shm_name->data = (unsigned char*)"stats_data";

    /* TODO calculate size more precisely. Now memory is allocated with huge reserved space */
    stats_data = ngx_shared_memory_add(cf, stats_data_shm_name, 28 * ngx_pagesize,
    		&ngx_http_ustats_module);

    if (!stats_data)
    	return NGX_CONF_ERROR;

    stats_data->init = ngx_http_ustats_init_stats_data_shm;

    return NGX_CONF_OK;
}



static ngx_buf_t * ngx_http_ustats_create_response_full_html(ngx_http_request_t * r)
{
    size_t size = 0;
    ngx_buf_t * b = NULL;
    time_t now;

    ngx_http_upstream_main_conf_t * conf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    now = ngx_time();

    // Calculate size
    size = sizeof(RESPONSE_START) +
           sizeof(RESPONSE_HEAD) +
           sizeof(RESPONSE_BODY_START) +
           sizeof("<table class=\"statsTable\" width=\"\" height=\"\">"
                       "<tr>"
                           "<td class=\"stcCommon\" align=\"left\" colspan=\"12\">Last update: "
                               "<script type=\"text/javascript\">"
                                   "var now = new Date();"
                                   "document.write(now.getHours() + ':' + now.getMinutes() + ':' + now.getSeconds());"
                               "</script>"
                           "</td>"
                       "</tr>"
                       "<tr>"
                           "<th class=\"stcCommon stcHeader\">Upstream</th>"
                           "<th class=\"stcCommon stcHeader\">Backend</th>"
                           "<th class=\"stcCommon stcHeader\">Requests</th>"
                           "<th class=\"stcCommon stcHeader\">Http 499</th>"
                           "<th class=\"stcCommon stcHeader\">Http 500</th>"
                           "<th class=\"stcCommon stcHeader\">Http 503</th>"
                           "<th class=\"stcCommon stcHeader\">Tcp<br/>errors</th>"
                           "<th class=\"stcCommon stcHeader\">Http read<br/>timeouts</th>"
                           "<th class=\"stcCommon stcHeader\">Http write<br/>timeouts</th>"
                           "<th class=\"stcCommon stcHeader\">Fail<br/>timeout, sec.</th>"
						   "<th class=\"stcCommon stcHeader\">Max<br/>fails</th>"
                           "<th class=\"stcCommon stcHeader\">Last fail</th>"
                       "</tr>"
                  "</table>") +
           sizeof(RESPONSE_BODY_END) +
           sizeof(RESPONSE_END);

    size += sizeof(ngx_uint_t); // refresh interval

    // add table width and height values
    ngx_http_ustats_loc_conf_t * uslc = ngx_http_get_module_loc_conf(r, ngx_http_ustats_module);

    size += sizeof(ngx_uint_t) * 2;
    if (uslc->html_table_width <= 100)
        size += sizeof("%%");
    if (uslc->html_table_width <= 100)
        size += sizeof("%%");

    unsigned i, k;

    for (i = 0; i < conf->upstreams.nelts; ++i)
    {
        ngx_http_upstream_srv_conf_t *uscf = ((ngx_http_upstream_srv_conf_t**)conf->upstreams.elts)[i];
        ngx_http_upstream_rr_peers_t *peers = uscf->peer.data;

        // upstream cell with rowspan variable parameter
        size += sizeof("<tr><th class=\"stcCommon stcHeader\" rowspan=\"\"></th>") +
                sizeof(ngx_uint_t);

        // Explicit or implicit
        if ((peers->number && (!peers->peer[0].server || !peers->peer[0].server->name.data)))
        	size += sizeof(" stcImplicitUpstream");

        // upstream name
        size += (uscf->host.len + 1 /* '\0' is not included in host.len */) * sizeof(u_char);

        // Peers

        for (k = 0; k < peers->number; ++k)
        {
            size += sizeof(
                        // peer name. Additional class may vary
						"<td class=\"stcCommon stcPeer\"></td>"
                        // reqs
                        "<td class=\"stcCommon\"></td>"
                        // 499
                        "<td class=\"stcCommon\"></td>"
                        // 500
                        "<td class=\"stcCommon\"></td>"
                        // 503
                        "<td class=\"stcCommon\"></td>"
                        // tcp errors
                        "<td class=\"stcCommon\"></td>"
                        // http read timeouts
                        "<td class=\"stcCommon\"></td>"
                        // http write timeouts
                        "<td class=\"stcCommon\"></td>"
                        // fail timeout
                        "<td class=\"stcCommon\"></td>"
						// max fails
                        "<td class=\"stcCommon\"></td>"
                        // last fail
                        "<td class=\"stcCommon\"></td>"
                    "</tr>");

            size += uscf->host.len + 1; // we put upstream name into id="" attribute

            // backend numeric parameters
            size += sizeof(ngx_uint_t) * 9;

            // failed access time string
            size += sizeof(u_char) * 24;
            if (((time_t*)stats_data->data)[peers->peer[k].shm_start_ind + USTATS_BLACKLISTED_STAT_INDEX] == 0)
            	size += sizeof(" stcNoLastFail");

            // blacklisted?
            if (now - ((time_t*)stats_data->data)
            		[peers->peer[k].shm_start_ind + USTATS_BLACKLISTED_STAT_INDEX] < peers->peer[k].fail_timeout)
            	size += sizeof(" stcBlacklisted") * 11; // each cell in row

            // disabled?
            if (peers->peer[k].down)
                size += sizeof(" stcDisabled") * 11; // each cell in row

            // write <peer_name> (server_conf_name || upstream_name) if there is more than one peer per backend
            if (!peers->peer[k].server || peers->peer[k].server->naddrs > 1)
            {
                size += (peers->peer[k].name.len + 1) * sizeof(u_char) +
                        sizeof("<br/>()");
                size += (!peers->peer[k].server)
							? (uscf->host.len + 1) * sizeof(u_char) // for implicit backends
							: (peers->peer[k].server->name.len + 1) * sizeof(u_char); // for the rest
            }
            // one server <-> one peer - write server config name
            else
            {
                size += (peers->peer[k].server->name.len + 1) * sizeof(u_char);
            }
        }

        // only first upstream peer row starts with upstream
        // name cell, others are separate rows
        size += sizeof("<tr>") * (peers->number - 1);
    }


    // Create buffer for response
    b = ngx_create_temp_buf(r->pool, size);


    /*************************************************/
    // Fill data
    /*************************************************/
    b->last = ngx_sprintf(b->last, RESPONSE_START);
    b->last = ngx_sprintf(b->last, RESPONSE_HEAD, (unsigned)uslc->refresh_interval);
    b->last = ngx_sprintf(b->last, RESPONSE_BODY_START);

    b->last = ngx_sprintf(b->last, "<table class=\"statsTable\" ");
    // table width
    b->last = (uslc->html_table_width <= 100)
                    ? ngx_sprintf(b->last, "width=\"%d%%\" ", uslc->html_table_width)
                    : ngx_sprintf(b->last, "width=\"%d\" ", uslc->html_table_width);

    // table height
    b->last = (uslc->html_table_height <= 100)
                    ? ngx_sprintf(b->last, "height=\"%d%%\" ", uslc->html_table_height)
                    : ngx_sprintf(b->last, "height=\"%d\" ", uslc->html_table_height);

    b->last = ngx_sprintf(b->last, ">");

    b->last = ngx_sprintf(b->last,
                                // updater timer row
                                "<tr>"
                                    "<td class=\"stcCommon\" align=\"left\" colspan=\"12\">Last update: "
                                        "<script type=\"text/javascript\">"
                                            "var now = new Date();"
                                            "document.write(now.getHours() + ':' + now.getMinutes() + ':' + now.getSeconds());"
                                        "</script>"
                                    "</td>"
                                "</tr>"
                                "<tr>"
                                    "<th class=\"stcCommon stcHeader\">Upstream</th>"
                                    "<th class=\"stcCommon stcHeader\">Backend</th>"
                                    "<th class=\"stcCommon stcHeader\">Requests</th>"
                                    "<th class=\"stcCommon stcHeader\">Http 499</th>"
                                    "<th class=\"stcCommon stcHeader\">Http 500</th>"
                                    "<th class=\"stcCommon stcHeader\">Http 503</th>"
                                    "<th class=\"stcCommon stcHeader\">Tcp<br/>errors</th>"
                                    "<th class=\"stcCommon stcHeader\">Http read<br/>timeouts</th>"
                                    "<th class=\"stcCommon stcHeader\">Http write<br/>timeouts</th>"
                                    "<th class=\"stcCommon stcHeader\">Fail<br/>timeout, sec.</th>"
									"<th class=\"stcCommon stcHeader\">Max<br/>fails</th>"
                                    "<th class=\"stcCommon stcHeader\">Last fail</th>"
                                "</tr>");

    // upstreams
    for (i = 0; i < conf->upstreams.nelts; ++i)
    {
        ngx_http_upstream_srv_conf_t *uscf = ((ngx_http_upstream_srv_conf_t**)conf->upstreams.elts)[i];
        ngx_http_upstream_rr_peers_t *peers = uscf->peer.data;

        int first_row = 1;

        // upstream name
        b->last = ngx_sprintf(b->last, "<tr><th class=\"stcCommon stcHeader%s\" rowspan=\"%uA\">%s</th>",
							  (peers->number && (!peers->peer[0].server || !peers->peer[0].server->name.data))
									  ? " stcImplicitUpstream" : "", peers->number, uscf->host.data);

        // upstream peers
        for (k = 0; k < peers->number; ++k)
        {
            // only first row starts with upstream cell, others are separate ones
            if (!first_row)
            {
                b->last = ngx_sprintf(b->last, "<tr>");
                first_row = 0;
            }

			unsigned blacklisted = now - ((time_t*)stats_data->data)
					[peers->peer[k].shm_start_ind + USTATS_BLACKLISTED_STAT_INDEX] < peers->peer[k].fail_timeout;
            unsigned disabled = (peers->peer[k].down) ? 1 : 0;

            // peer name cell
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon stcPeer%s%s\">",
					blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "", uscf->host.data);

            // OMG
            if (!peers->peer[k].server || peers->peer[k].server->naddrs > 1)
            {
                // Dirty workaround. Strange bugs with nginx-created strings when running under Duma
                size_t j;
                // Peer name (resolved)
                for (j = 0; j < peers->peer[k].name.len; ++j)
                    b->last = ngx_sprintf(b->last, "%c", peers->peer[k].name.data[j]);
                // Initial config name (unresolved)
                b->last = (!peers->peer[k].server)
								? ngx_sprintf(b->last, "<br/>(%s)", uscf->host.data)
								: ngx_sprintf(b->last, "<br/>(%s)", (char*)peers->peer[k].server->name.data);
            }
            else
            {
            	b->last = ngx_sprintf(b->last, "%s", (peers->peer[k].server->name.data
            			? (char*)peers->peer[k].server->name.data
            			: (char*)uscf->host.data));
            }

            b->last = ngx_sprintf(b->last, "</td>");

            // reqs
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
					(int)((ngx_atomic_uint_t*)stats_data->data)[peers->peer[k].shm_start_ind +
																			USTATS_REQ_STAT_INDEX]);

            // 499s
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
					(int)((ngx_atomic_uint_t*)stats_data->data)[peers->peer[k].shm_start_ind +
					                                            USTATS_HTTP499_STAT_INDEX]);

            // 500s
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
					(int)((ngx_atomic_uint_t*)stats_data->data)[peers->peer[k].shm_start_ind +
					                                            USTATS_HTTP500_STAT_INDEX]);

            // 503s
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
					(int)((ngx_atomic_uint_t*)stats_data->data)[peers->peer[k].shm_start_ind +
					                                            USTATS_HTTP503_STAT_INDEX]);

            // tcp errors
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
					(int)((ngx_atomic_uint_t*)stats_data->data)[peers->peer[k].shm_start_ind +
					                                            USTATS_TCP_ERR_STAT_INDEX]);

            // http read timeouts
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
					(int)((ngx_atomic_uint_t*)stats_data->data)[peers->peer[k].shm_start_ind +
					                                            USTATS_READ_TIMEOUT_STAT_INDEX]);

            // http write timeouts
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
					(int)((ngx_atomic_uint_t*)stats_data->data)[peers->peer[k].shm_start_ind +
																USTATS_WRITE_TIMEOUT_STAT_INDEX]);

            // fail timeout
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].fail_timeout);

            // max fails
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].max_fails);

            // last fail time
            if (((time_t*)stats_data->data)[peers->peer[k].shm_start_ind + USTATS_BLACKLISTED_STAT_INDEX] == 0)
            {
                b->last = ngx_sprintf(b->last, "<td class=\"stcCommon stcNoLastFail%s%s\">0000-00-00 00:00:00</td></tr>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "");
            }
            else
            {
                char at_b[30];
                struct tm * t = localtime(&((time_t*)stats_data->data)
                		[peers->peer[k].shm_start_ind + USTATS_BLACKLISTED_STAT_INDEX]);
                strftime(at_b, sizeof(at_b), "%Y-%m-%d %H:%M:%S", t);
                b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%s</td></tr>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "", at_b);
            }
        }
    }

    b->last = ngx_sprintf(b->last, "</table>%s%s", RESPONSE_BODY_END, RESPONSE_END);

    return b;
}
