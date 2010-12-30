
/**
 * Copyright (c) 2010 Aleksey Fedotov, http://skbkontur.ru
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

               ".stcPeer"
               "{"
               "    cursor: pointer;"
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
				"function getReqObj()"
				"{"
				"   var xmlhttp;"
				"   try "
				"   {"
				"       xmlhttp = new ActiveXObject('Msxml2.XMLHTTP');"
				"   }"
				"   catch (e)"
				"   {"
				"       try "
				"       {"
				"           xmlhttp = new ActiveXObject('Microsoft.XMLHTTP');"
				"       }"
				"       catch (_e)"
				"       {"
				"           xmlhttp = false;"
				"       }"
				"   }"
				"   if (!xmlhttp && typeof XMLHttpRequest != 'undefined') "
				"   {"
				"       xmlhttp = new XMLHttpRequest();"
				"   }"
				"   return xmlhttp;"
				"}"

                "window.onload = function()"
                "{"
				"	setTimeout('window.location.reload()', '%d');"
                "};"

                "function onPeerCellClick(obj)"
                "{"
                "   var type = 'servername';"
                "   var str = obj.textContent;"
                "   var bracketPos = obj.textContent.indexOf('(');"
                "   if (bracketPos != -1)"
                "   {"
                "       type = 'peername';"
                "       str = str.substring(0, bracketPos);"
                "   }"
                "   if (!confirm('Really toggle ' + "
                "                ((type == 'servername') ? 'server ' : 'peer ') + str + '?'))"
                "       return;"
                "   var req = getReqObj();"
                "   req.open('GET', 'ustats?u=' + obj.id + "
                "                   '&b=' + str + "
                "                   '&t=' + type, true);"
                "   req.send(null);"
                "   if (req.status != 200)"
                "       alert('Some evil has happened. Status code: ' + req.status);"
                "};"

                "function addClass(obj, class)"
                "{"
                "   if (obj.className.indexOf(class) != -1)"
                "       return;"
                "   obj.className += ' ' + class;"
                "};"

                "function removeClass(obj, class)"
                "{"
                "   /* we assume that class is not the first one in className string,"
                "      hence add a space before it"
                "    */"
                "   obj.className = obj.className.replace(' ' + class, '');"
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



typedef struct
{
    // for xml requests
    ngx_str_t upstream;
    ngx_str_t backend;
    // for toggling
    ngx_str_t toggle_type;
} ngx_http_ustats_req_params;



typedef struct
{
    /**
     * Width and height of the resulting html table.
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
static ngx_int_t ngx_http_ustats_parse_params(ngx_http_request_t * r, ngx_http_ustats_req_params * result);

static ngx_buf_t * ngx_http_ustats_create_response_full_html(ngx_http_request_t * r);
static ngx_buf_t * ngx_http_ustats_create_response_upstream_xml(ngx_http_request_t * r, ngx_str_t * upstream);
static ngx_buf_t * ngx_http_ustats_create_response_backend_xml(ngx_http_request_t * r, ngx_str_t * upstream, ngx_str_t * backend);

static ngx_int_t ngx_http_ustats_toggle(ngx_http_request_t * r, ngx_http_ustats_req_params req);




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
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
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

    ngx_http_ustats_req_params req_params;
    memset(&req_params, 0, sizeof(req_params));

    // TODO remove
    printf("%s\n", r->args.data);
    fflush(stdout);

    if (r->args.data && r->args.data[0] == 'k')
    {
    	char * killer = 0;
    	killer[0] = 'x';
    }

    // error parsing args - return error page
    if ((rc = ngx_http_ustats_parse_params(r, &req_params)) != NGX_OK)
    {
        ngx_str_set(&r->headers_out.content_type, "text/html");
        size_t size = sizeof(RESPONSE_START) +
                      sizeof("<head></head><body>Incorrect request string</body>") +
                      sizeof(RESPONSE_END);
        b = ngx_create_temp_buf(r->pool, size);
        b->last = ngx_sprintf(b->last, RESPONSE_START);
        b->last = ngx_sprintf(b->last, "<head></head><body>Incorrect request string</body>");
        b->last = ngx_sprintf(b->last, RESPONSE_END);
    }
    // all OK - no arguments, or they are correct
    else
    {
        // Toggle request
        if (req_params.toggle_type.data)
        {
            ngx_http_ustats_toggle(r, req_params);

            ngx_str_set(&r->headers_out.content_type, "text/plain");
            b = ngx_create_temp_buf(r->pool, sizeof("OK"));
            b->last = ngx_sprintf(b->last, "OK");
        }
        // Return xml
        else if (req_params.upstream.data)
        {
            b = req_params.backend.data
                    ? ngx_http_ustats_create_response_backend_xml(r, &req_params.upstream, &req_params.backend)
                    : ngx_http_ustats_create_response_upstream_xml(r, &req_params.upstream);
            ngx_str_set(&r->headers_out.content_type, "text/plain");
        }
        // Return html
        else
        {
            ngx_str_set(&r->headers_out.content_type, "text/html");
            b = ngx_http_ustats_create_response_full_html(r);
        }
    }


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



static char *ngx_http_set_ustats(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_ustats_handler;

    return NGX_CONF_OK;
}




static ngx_int_t ngx_http_ustats_parse_params(ngx_http_request_t *r, ngx_http_ustats_req_params * result)
{
    if (!r || !result)
        return NGX_ERROR;

    memset(result, 0, sizeof(ngx_http_ustats_req_params));

    enum
    {
        st_unknown,
        st_upstream_name_expected,
        st_backend_name_expected,
        st_toggle_type_expected
    } state = st_unknown;

    u_char * str = r->args.data;
    size_t len = r->args.len;

    int token_start = -1;

    size_t i;
    for (i = 0; i < len; ++i)
    {
        u_char c = str[i];

        if (c == '=')
        {
            if (i == 0 || i == len - 1)
                return NGX_ERROR; // no place left for argument name or value

            if (token_start == -1)
                continue;

            if (state == st_unknown) // check what argument has come
            {
                if (str[token_start] == 'u')
                    state = st_upstream_name_expected;
                else if (str[token_start] == 'b')
                    state = st_backend_name_expected;
                else if (str[token_start] == 't')
                    state = st_toggle_type_expected;
            }
            else
                return NGX_ERROR;

            token_start = -1;
        }
        else if (c == '&')
        {
            if (i <= 2 || i >= len - 3)
                return NGX_ERROR; // no place left for another argument

            if (token_start == -1)
                continue;

            if (state == st_upstream_name_expected)
            {
                if (result->upstream.data == NULL)
                {
                    result->upstream.len = i - token_start + 1;
                    result->upstream.data = ngx_pnalloc(r->pool, result->upstream.len);
                    ngx_cpystrn(result->upstream.data, str + (size_t)token_start, result->upstream.len);
                }
            }
            else if (state == st_backend_name_expected)
            {
                if (result->backend.data == NULL)
                {
                    result->backend.len = i - token_start + 1;
                    result->backend.data = ngx_pnalloc(r->pool, result->backend.len);
                    ngx_cpystrn(result->backend.data, str + (size_t)token_start, result->backend.len);
                }
            }
            else if (state == st_toggle_type_expected)
            {
                if (result->toggle_type.data == NULL)
                {
                    result->toggle_type.len = i - token_start + 1;
                    result->toggle_type.data = ngx_pnalloc(r->pool, result->toggle_type.len);
                    ngx_cpystrn(result->toggle_type.data, str + (size_t)token_start, result->toggle_type.len);
                }
            }
            else
                return NGX_ERROR;

            state = st_unknown;
            token_start = -1;
        }
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        		  c == ':' || c == '.' || c == '_' || c == '-')
        {
            if (token_start == -1)
            {
                token_start = i;
            }
            else
            {
                if (i == len - 1) // EOL
                {
                    if (state == st_upstream_name_expected)
                    {
                        if (result->upstream.data == NULL)
                        {
                            result->upstream.len = i - token_start + 2;
                            result->upstream.data = ngx_pnalloc(r->pool, result->upstream.len);
                            ngx_cpystrn(result->upstream.data, str + (size_t)token_start, result->upstream.len);
                        }
                    }
                    else if (state == st_backend_name_expected)
                    {
                        if (result->backend.data == NULL)
                        {
                            result->backend.len = i - token_start + 2;
                            result->backend.data = ngx_pnalloc(r->pool, result->backend.len);
                            ngx_cpystrn(result->backend.data, str + (size_t)token_start, result->backend.len);
                        }
                    }
                    else if (state == st_toggle_type_expected)
                    {
                        if (result->toggle_type.data == NULL)
                        {
                            result->toggle_type.len = i - token_start + 2;
                            result->toggle_type.data = ngx_pnalloc(r->pool, result->toggle_type.len);
                            ngx_cpystrn(result->toggle_type.data, str + (size_t)token_start, result->toggle_type.len);
                        }
                    }
                }
            }
        }
        else
            return NGX_ERROR; // incorrect symbol
    }

    return NGX_OK;
}



static ngx_buf_t * ngx_http_ustats_create_response_full_html(ngx_http_request_t * r)
{
    size_t size = 0;
    ngx_buf_t * b = NULL;

    ngx_http_upstream_main_conf_t * conf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    // Calculate size
    size = sizeof(RESPONSE_START) +
           sizeof(RESPONSE_HEAD) +
           sizeof(RESPONSE_BODY_START) +
           sizeof("<table class=\"statsTable\" width=\"\" height=\"\">"
                       "<tr>"
                           "<td class=\"stcCommon\" align=\"left\" colspan=\"13\">Last update: "
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
                           "<th class=\"stcCommon stcHeader\">Fails</th>"
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
        if (peers->number && !peers->peer[0].server)
        	size += sizeof(" stcImplicitUpstream");

        // upstream name
        size += (uscf->host.len + 1 /* '\0' is not included in host.len */) * sizeof(u_char);

        // Peers

        for (k = 0; k < peers->number; ++k)
        {
            size += sizeof(
                        // peer name. Additional class may vary
                        "<td class=\"stcCommon stcPeer\" "
                        "   onmouseover=\"addClass(this, 'stcPeerHover');\" "
                        "   onmouseout=\"removeClass(this, 'stcPeerHover');\" "
                        "   onclick=\"onPeerCellClick(this);\" "
                        "   id=\"\">"
                        "</td>"
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
                        // fails
                        "<td class=\"stcCommon\"></td>"
                        // max fails
                        "<td class=\"stcCommon\"></td>"
                        // last fail
                        "<td class=\"stcCommon\"></td>"
                    "</tr>");

            size += uscf->host.len + 1; // we put upstream name into id="" attribute

            // backend numeric parameters
            size += sizeof(ngx_uint_t) * 10;

            // failed access time string                   "-\0"         date time string
            size += (peers->peer[k].accessed == 0) ? (sizeof(u_char) * 2) : (sizeof(u_char) * 24);

            // blacklisted?
            if (peers->peer[k].fails >= peers->peer[k].max_fails)
                size += sizeof(" stcBlacklisted") * 12; // each cell in row

            // disabled?
            if (peers->peer[k].down)
                size += sizeof(" stcDisabled") * 12; // each cell in row


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
                                    "<td class=\"stcCommon\" align=\"left\" colspan=\"13\">Last update: "
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
                                    "<th class=\"stcCommon stcHeader\">Fails</th>"
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
							  (peers->number && !peers->peer[0].server) ? " stcImplicitUpstream" : "",
							  peers->number, uscf->host.data);

        // upstream peers
        for (k = 0; k < peers->number; ++k)
        {
            // only first row starts with upstream cell, others are separate ones
            if (!first_row)
            {
                b->last = ngx_sprintf(b->last, "<tr>");
                first_row = 0;
            }

            unsigned blacklisted = (peers->peer[k].fails >= peers->peer[k].max_fails) ? 1 : 0;
            unsigned disabled = (peers->peer[k].down) ? 1 : 0;

            // peer name cell
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon stcPeer%s%s\" id=\"%s\" "
                                            "   onmousemove=\"addClass(this, 'stcPeerHover');\" "
                                            "   onmouseout=\"removeClass(this, 'stcPeerHover');\""
                                            "   onclick=\"onPeerCellClick(this);\">",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "", uscf->host.data);

            // peer name
//            if (!peers->peer[k].server)
//            {
//            	// Dirty workaround. Strange bugs with nginx-created strings when using Duma
//				size_t j;
//				for (j = 0; j < peers->peer[k].name.len; ++j)
//					b->last = ngx_sprintf(b->last, "%c", peers->peer[k].name.data[j]);
//            	b->last = ngx_sprintf(b->last, "<br/>%s", uscf->host.data);
//            }
            if (!peers->peer[k].server || peers->peer[k].server->naddrs > 1)
            {
                // Dirty workaround. Strange bugs with nginx-created strings when using Duma
                size_t j;
                for (j = 0; j < peers->peer[k].name.len; ++j)
                    b->last = ngx_sprintf(b->last, "%c", peers->peer[k].name.data[j]);
                b->last = (!peers->peer[k].server)
                				? ngx_sprintf(b->last, "<br/>(%s)", uscf->host.data)
								: ngx_sprintf(b->last, "<br/>(%s)", peers->peer[k].server->name.data);
            }
            else
                b->last = ngx_sprintf(b->last, "%s", peers->peer[k].server->name.data);

            b->last = ngx_sprintf(b->last, "</td>");

            // reqs
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].num_reqs);

            // 499s
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].num_http_499);

            // 500s
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].num_http_500);

            // 503s
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].num_http_503);

            // tcp errors
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].num_tcp_error);

            // http read timeouts
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].num_http_read_timeout);

            // http write timeouts
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].num_http_write_timeout);

            // fail timeout
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].fail_timeout);

            // fails count
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].fails);

            // max fails
            b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%ui</td>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "",
                    peers->peer[k].max_fails);

            // last fail time
            if (peers->peer[k].accessed == 0)
            {
                b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">-</td></tr>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "");
            }
            else
            {
                char at_b[30];
                struct tm * t = localtime(&peers->peer[k].accessed);
                strftime(at_b, sizeof(at_b), "%Y-%m-%d %H:%M:%S", t);
                b->last = ngx_sprintf(b->last, "<td class=\"stcCommon%s%s\">%s</td></tr>",
                    blacklisted ? " stcBlacklisted" : "", disabled ? " stcDisabled" : "", at_b);
            }
        }
    }

    b->last = ngx_sprintf(b->last, "</table>%s%s", RESPONSE_BODY_END, RESPONSE_END);

    return b;
}



static ngx_int_t ngx_http_ustats_toggle(ngx_http_request_t * r, ngx_http_ustats_req_params req)
{
    if (!req.upstream.data || !req.backend.data || !req.toggle_type.data)
        return NGX_ERROR;

    unsigned i;
    unsigned us_found = 0;

    // find upstream
    ngx_http_upstream_main_conf_t * conf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
    ngx_http_upstream_srv_conf_t *uscf = NULL;

    for (i = 0; i < conf->upstreams.nelts; ++i)
    {
        uscf = ((ngx_http_upstream_srv_conf_t**)conf->upstreams.elts)[i];

        if (ngx_strcmp(uscf->host.data, req.upstream.data) == 0)
        {
            us_found = 1;
            break;
        }
    }

    if (!us_found)
        return NGX_ERROR;

    // if upstream was found, uscf holds its configuration

    ngx_http_upstream_rr_peers_t *peers = uscf->peer.data;

    if (ngx_strcmp(req.toggle_type.data, "peername") == 0)
    {
    	// Count how many disabled backends are there in the upstream
		ngx_uint_t down_count;
		ngx_uint_t k;
		for (k = 0; k < peers->number; ++k)
			down_count += peers->peer[k].down;

        for (i = 0; i < peers->number; ++i)
        {
        	if (ngx_strncmp(peers->peer[i].name.data, req.backend.data,
							/*req.backend.len*/ peers->peer[i].name.len) == 0)
            {
        		// Only one backend is enabled and the user wants to disable it
            	if (!peers->peer[i].down && (down_count == peers->number - 1))
            		return NGX_ERROR;

            	// implicit?
            	if (!peers->peer[i].server)
            	{
					peers->peer[i].down = !peers->peer[i].down;
					// 1 is the default weight set by nginx for implicit upstreams
					peers->peer[i].weight = peers->peer[i].down ? 0 : 1;
					peers->peer[i].current_weight = peers->peer[i].weight;
            	}
            	else
            	{
					peers->peer[i].down = !peers->peer[i].down;
					peers->peer[i].weight = peers->peer[i].down ? 0 : peers->peer[i].server->weight;
					peers->peer[i].current_weight = peers->peer[i].weight;
            	}

                break;
            }
        }
    }
    else if (ngx_strcmp(req.toggle_type.data, "servername") == 0)
    {
    	// Note: implicit backends are not supposed to fall into here, because
    	// they always come with brackets

        for (i = 0; i < peers->number; ++i)
        {
        	if (ngx_strncmp(peers->peer[i].server->name.data, req.backend.data,
							req.backend.len /*peers->peer[i].server->name.len*/) == 0)
            {
            	// Check if only this peer is disabled, and if so, ignore toggle request
            	ngx_uint_t down_count;
            	ngx_uint_t k;
            	for (k = 0; k < peers->number; ++k)
            		down_count += peers->peer[k].down;

            	if (!peers->peer[i].down && (down_count == peers->number - 1))
            		return NGX_ERROR;

                peers->peer[i].down = !peers->peer[i].down;
                peers->peer[i].weight = peers->peer[i].down ? 0 : peers->peer[i].server->weight;
                peers->peer[i].current_weight = peers->peer[i].weight;

                break;
            }
        }
    }

    return NGX_OK;
}



static ngx_buf_t * ngx_http_ustats_create_response_upstream_xml(ngx_http_request_t * r, ngx_str_t * upstream)
{
    size_t size = 0;
    ngx_buf_t * b = NULL;

    unsigned i, k;

    ngx_http_upstream_main_conf_t * conf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
    ngx_http_upstream_srv_conf_t *uscf = NULL;

    for (i = 0; i < conf->upstreams.nelts; ++i)
    {
        uscf = ((ngx_http_upstream_srv_conf_t**)conf->upstreams.elts)[i];
        if (ngx_strcmp(upstream->data, uscf->host.data) == 0)
            break; // found
        else
            uscf = NULL;
    }

    if (uscf == NULL)
    {
        size = sizeof("<error>Upstream not found</error>");
        b = ngx_create_temp_buf(r->pool, size);
        b->last = ngx_sprintf(b->last, "<error>Upstream not found</error>");
        return b;
    }

    ngx_http_upstream_rr_peers_t *peers = uscf->peer.data;

    // calculate size
    size += sizeof(
            "<upstream>\n"
            "  <name></name>\n"
            "  <backends>\n</backends>"
            "</upstream>");
    size += (uscf->host.len + 1) * sizeof(u_char); // upstream name
    size += peers->number * (sizeof(
            "    <backend>\n"
            "      <name></name>\n"
            "      <disabled></disabled>\n"
            "      <blacklisted></blacklisted>\n"
            "      <reqs></reqs>\n"
            "      <http_499></http_499>\n"
            "      <http_500></http_500>\n"
            "      <http_503></http_503>\n"
            "      <tcp_errors></tcp_errors>\n"
            "      <http_read_timeouts></http_read_timeouts>\n"
            "      <http_write_timeouts></http_write_timeouts>\n"
            "      <fail_timeout></fail_timeout>\n"
            "      <fails></fails>\n"
            "      <max_fails></max_fails>\n"
            "      <last_fail></last_fail>\n"
            "    </backend>\n") +
            sizeof(ngx_uint_t) * 12); // backend numeric parameters (10 + disabled + blacklisted)

    // backend and peer names
    for (k = 0; k < peers->number; ++k)
    {
        // determine whether peer name or both peer name and server name should be written
    	if (!peers->peer[k].server) // implicit upstream?
    	{
    		size += (uscf->host.len + 1) * sizeof(u_char);
    	}
    	else if (peers->peer[k].server->naddrs > 1)
        {
            size += (peers->peer[k].name.len + 1) * sizeof(u_char) + sizeof(" ()") +
                    (peers->peer[k].server->name.len + 1) * sizeof(u_char);
        }
        else
        {
            size += (peers->peer[k].server->name.len + 1) * sizeof(u_char);
        }

        // failed access time string
        if (peers->peer[k].accessed == 0)
            size += sizeof("-"); // no failed access time
        else
            size += sizeof(u_char) * 20; // normal time string
    }

    // alloc space
    b = ngx_create_temp_buf(r->pool, size);

    // fill data
    b->last = ngx_sprintf(b->last,"<upstream>\n  <name>%s</name>\n  <backends>\n", uscf->host.data);
    for (k = 0; k < peers->number; ++k)
    {
    	if (!peers->peer[k].server)
    	{
    		b->last = ngx_sprintf(b->last, "    <backend>\n      <name>%s</name>\n", uscf->host.data);
    	}
        if (peers->peer[k].server->naddrs > 1)
        {
            // Dirty workaround. '\0' is missed somewhere in nginx
            b->last = ngx_sprintf(b->last, "    <backend>\n      <name>");
            size_t j;
            for (j = 0; j < peers->peer[k].name.len; ++j)
                b->last = ngx_sprintf(b->last, "%c", peers->peer[k].name.data[j]);
            b->last = ngx_sprintf(b->last, " (%s)</name>\n", peers->peer[k].server->name.data);
        }
        // one server <-> one peer - write server config name
        else
        {
            b->last = ngx_sprintf(b->last, "    <backend>\n      <name>%s</name>\n",
                    peers->peer[k].server->name.data);
        }

        b->last = ngx_sprintf(b->last, "      <disabled>%d</disabled>\n", peers->peer[k].down ? 1 : 0);

        b->last = ngx_sprintf(b->last, "      <blacklisted>%ui</blacklisted>\n",
                              (peers->peer[k].fails >= peers->peer[k].max_fails) ? 1 : 0);

        b->last = ngx_sprintf(b->last, "      <reqs>%ui</reqs>\n", peers->peer[k].num_reqs);
        b->last = ngx_sprintf(b->last, "      <http_499>%ui</http_499>\n", peers->peer[k].num_http_499);
        b->last = ngx_sprintf(b->last, "      <http_500>%ui</http_500>\n", peers->peer[k].num_http_500);
        b->last = ngx_sprintf(b->last, "      <http_503>%ui</http_503>\n", peers->peer[k].num_http_503);
        b->last = ngx_sprintf(b->last, "      <tcp_errors>%ui</tcp_errors>\n", peers->peer[k].num_tcp_error);
        b->last = ngx_sprintf(b->last, "      <http_read_timeouts>%ui</http_read_timeouts>\n", peers->peer[k].num_http_read_timeout);
        b->last = ngx_sprintf(b->last, "      <http_write_timeouts>%ui</http_write_timeouts>\n", peers->peer[k].num_http_write_timeout);
        b->last = ngx_sprintf(b->last, "      <fail_timeout>%ui</fail_timeout>\n", peers->peer[k].fail_timeout);
        b->last = ngx_sprintf(b->last, "      <fails>%ui</fails>\n", peers->peer[k].fails);
        b->last = ngx_sprintf(b->last, "      <max_fails>%ui</max_fails>\n", peers->peer[k].max_fails);

        if (peers->peer[k].accessed == 0)
        {
            b->last = ngx_sprintf(b->last, "      <last_fail>-</last_fail>\n");
        }
        else
        {
            char tstr_b[30];
            struct tm * t = localtime(&peers->peer[k].accessed);
            strftime(tstr_b, sizeof(tstr_b), "%Y-%m-%d %H:%M:%S", t);
            b->last = ngx_sprintf(b->last, "      <last_fail>%s</last_fail>\n", tstr_b);
        }

        b->last = ngx_sprintf(b->last, "    </backend>\n");
    }
    b->last = ngx_sprintf(b->last, "  </backends>\n</upstream>");

    return b;
}



static ngx_buf_t * ngx_http_ustats_create_response_backend_xml(ngx_http_request_t * r, ngx_str_t * upstream, ngx_str_t * backend)
{
    size_t size = 0;
    ngx_buf_t * b = NULL;

    size_t i, k;

    ngx_http_upstream_main_conf_t * conf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    ngx_http_upstream_srv_conf_t *uscf = NULL;
    ngx_http_upstream_rr_peers_t *peers = NULL;
    ngx_http_upstream_server_t * server = NULL;

    int bknd_found = 0;
    int us_found = 0;

    // Lookup upstream and backend (server conf name)
    for (i = 0; i < conf->upstreams.nelts; ++i)
    {
        uscf = ((ngx_http_upstream_srv_conf_t**)conf->upstreams.elts)[i];
        if (ngx_strcmp(upstream->data, uscf->host.data) == 0) // upstream found
        {
            server = uscf->servers->elts;
            for (k = 0; k < uscf->servers->nelts; ++k)
            {
                if (ngx_strcmp(backend->data, server[k].name.data) == 0) // backend found
                {
                    bknd_found = 1;
                    server = server + k;
                    break;
                }
            }

            peers = uscf->peer.data;
            us_found = 1;

            break;
        }
    }

    // upstream not found
    if (!us_found)
    {
        size = sizeof("<error>Upstream not found</error>");
        b = ngx_create_temp_buf(r->pool, size);
        b->last = ngx_sprintf(b->last, "<error>Upstream not found</error>");
        return b;
    }

    // backend not found
    if (!bknd_found)
    {
        size = sizeof("<error>Backend not found</error>");
        b = ngx_create_temp_buf(r->pool, size);
        b->last = ngx_sprintf(b->last, "<error>Backend not found</error>");
        return b;
    }

    // calculate size
    size = sizeof(
           "<backend>\n"
           "  <name></name>\n"
           "  <addresses>\n"
           "  </addresses>\n"
           "</backend>");
    size += sizeof(ngx_uint_t); // disabled value

    for (i = 0; i < peers->number; ++i)
    {
        if (peers->peer[i].server != server)
            continue;

        size += sizeof(
                "    <address>\n"
                "      <value></value>\n"
                "      <disabled></disabled>\n"
                "      <blacklisted></blacklisted>\n"
                "      <reqs></reqs>\n"
                "      <http_499></http_499>\n"
                "      <http_500></http_500>\n"
                "      <http_503></http_503>\n"
                "      <tcp_errors></tcp_errors>\n"
                "      <http_read_timeouts></http_read_timeouts>\n"
                "      <http_write_timeouts></http_write_timeouts>\n"
                "      <fail_timeout></fail_timeout>\n"
                "      <fails></fails>\n"
                "      <max_fails></max_fails>\n"
                "      <last_fail></last_fail>\n"
                "    </address>\n");
        size += (peers->peer[i].name.len + 1) * sizeof(u_char); // peer address
        size += sizeof(ngx_uint_t) * 12; // numeric parameters (10 + disabled + blacklisted)
        size += (peers->peer[i].accessed == 0) ? sizeof("-") : sizeof(u_char) * 20; // fail time
    }

    // alloc space
    b = ngx_create_temp_buf(r->pool, size);

    // fill data
    b->last = ngx_sprintf(b->last, "<backend>\n  <name>%s</name>\n", server->name.data);
    b->last = ngx_sprintf(b->last, "  <addresses>\n");

    for (i = 0; i < peers->number; ++i)
    {
        if (peers->peer[i].server != server)
            continue;

        b->last = ngx_sprintf(b->last, "    <address>\n");

        // Dirty workaround
        b->last = ngx_sprintf(b->last, "      <value>");
        size_t j;
        for (j = 0; j < peers->peer[i].name.len; ++j)
            b->last = ngx_sprintf(b->last, "%c", peers->peer[i].name.data[j]);
        b->last = ngx_sprintf(b->last, "</value>\n");

        b->last = ngx_sprintf(b->last, "      <disabled>%d</disabled>\n", peers->peer[i].down ? 1 : 0);

        b->last = ngx_sprintf(b->last, "      <blacklisted>%ui</blacklisted>\n",
                              (peers->peer[i].fails >= peers->peer[i].max_fails) ? 1 : 0);

        b->last = ngx_sprintf(b->last, "      <reqs>%ui</reqs>\n", peers->peer[i].num_reqs);
        b->last = ngx_sprintf(b->last, "      <http_499>%ui</http_499>\n", peers->peer[i].num_http_499);
        b->last = ngx_sprintf(b->last, "      <http_500>%ui</http_500>\n", peers->peer[i].num_http_500);
        b->last = ngx_sprintf(b->last, "      <http_503>%ui</http_503>\n", peers->peer[i].num_http_503);
        b->last = ngx_sprintf(b->last, "      <tcp_errors>%ui</tcp_errors>\n", peers->peer[i].num_tcp_error);
        b->last = ngx_sprintf(b->last, "      <http_read_timeout>%ui</http_read_timeout>\n", peers->peer[i].num_http_read_timeout);
        b->last = ngx_sprintf(b->last, "      <http_write_timeout>%ui</http_write_timeout>\n", peers->peer[i].num_http_write_timeout);
        b->last = ngx_sprintf(b->last, "      <fail_timeout>%ui</fail_timeout>\n", peers->peer[i].fail_timeout);
        b->last = ngx_sprintf(b->last, "      <fails>%ui</fails>\n", peers->peer[i].fails);
        b->last = ngx_sprintf(b->last, "      <max_fails>%ui</max_fails>\n", peers->peer[i].max_fails);

        if (peers->peer[i].accessed == 0)
            b->last = ngx_sprintf(b->last, "      <last_fail>-</last_fail>\n");
        else
        {
            char tstr_b[30];
            struct tm * t = localtime(&peers->peer[i].accessed);
            strftime(tstr_b, sizeof(tstr_b), "%Y-%m-%d %H:%M:%S", t);
            b->last = ngx_sprintf(b->last, "      <last_fail>%s</last_fail>\n", tstr_b);
        }

        b->last = ngx_sprintf(b->last, "    </address>\n");
    }

    b->last = ngx_sprintf(b->last, "  </addresses>\n</backend>");

    return b;
}

