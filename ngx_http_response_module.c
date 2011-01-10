
/*
 * Copyright (C) Kirill A. Korinskiy
 */

#include <nginx.h>

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
    ngx_str_t     response;
    ngx_array_t  *response_lengths;
    ngx_array_t  *response_values;
    ngx_str_t     type;
} ngx_http_response_conf_t;

static char *ngx_http_response_merge_conf(ngx_conf_t *cf, void *parent, void *child);
static void *ngx_http_response_create_conf(ngx_conf_t *cf);
static char *ngx_http_response_set_response_slot(ngx_conf_t *cf, ngx_command_t *cmd,
                                                 void *conf);


static ngx_command_t  ngx_http_response_commands[] = {

    { ngx_string("response"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_response_set_response_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("response_type"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_response_conf_t, type),
      NULL },

    ngx_null_command
};


static ngx_http_module_t  ngx_http_response_module_ctx = {
    NULL,                          /* preconfiguration */
    NULL,                          /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */

    ngx_http_response_create_conf, /* create location configuration */
    ngx_http_response_merge_conf   /* merge location configuration */
};


ngx_module_t  ngx_http_response_module = {
    NGX_MODULE_V1,
    &ngx_http_response_module_ctx, /* module context */
    ngx_http_response_commands,    /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_response_handler(ngx_http_request_t *r)
{
    ngx_int_t                     rc;
    ngx_buf_t                    *b;
    ngx_str_t                     response;
    ngx_chain_t                   out;
    ngx_http_script_code_pt       code;
    ngx_http_script_engine_t      e;
    ngx_http_response_conf_t     *conf;
    ngx_http_core_loc_conf_t     *clcf;
    ngx_http_script_len_code_pt   lcode;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_response_module);

    if (!conf->response.len) {
        return NGX_DECLINED;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    if (conf->type.len) {
        r->headers_out.content_type = conf->type;
    } else {
        r->headers_out.content_type = clcf->default_type;
    }

    if (conf->response_lengths == NULL &&
        conf->response_values == NULL) {
        response = conf->response;
    } else {
        ngx_memzero(&e, sizeof(ngx_http_script_engine_t));
        e.ip = conf->response_lengths->elts;
        e.request = r;
        e.flushed = 1;

        response.len = 1;                /* 1 byte for terminating '\0' */

        while (*(uintptr_t *) e.ip) {
            lcode = *(ngx_http_script_len_code_pt *) e.ip;
            response.len += lcode(&e);
        }

        response.data = ngx_pcalloc(r->pool, response.len);
        if (response.data == NULL) {
            return NGX_ERROR;
        }

        e.pos = response.data;
        e.ip = conf->response_values->elts;

        while (*(uintptr_t *) e.ip) {
            code = *(ngx_http_script_code_pt *) e.ip;
            code((ngx_http_script_engine_t *) &e);
        }

        response.len = e.pos - response.data;
    }

    if (r->method == NGX_HTTP_HEAD) {
        r->headers_out.status = NGX_HTTP_OK;
        r->headers_out.content_length_n = response.len;

        return ngx_http_send_header(r);
    }

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    b->pos = response.data;
    b->last = response.data + response.len;
    b->memory = 1;
    b->last_buf = 1;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = response.len;

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}


static void *
ngx_http_response_create_conf(ngx_conf_t *cf)
{
    ngx_http_response_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_response_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->response = { 0, NULL };
     *     conf->response_lengths = NULL;
     *     conf->response_values = NULL;
     *     conf->type = { 0, NULL };
     */

    return conf;
}


static char *
ngx_http_response_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_response_conf_t *prev = parent;
    ngx_http_response_conf_t *conf = child;

    if (prev->response.len) {
        conf->response = prev->response;
        conf->response_values = prev->response_values;
        conf->response_lengths = prev->response_lengths;
    }

    ngx_conf_merge_str_value(conf->type, prev->type, "");

    return NGX_CONF_OK;
}

static char *
ngx_http_response_set_response_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_response_conf_t   *hrct = conf;
    ngx_str_t                  *value;
    ngx_uint_t                  n;
    ngx_http_script_compile_t   sc;

#if defined(nginx_version) && nginx_version >= 8042

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "the \"response\" directive is deprecated, "
                       "use the \"return\" directive instead");

#endif

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_response_handler;

    if (hrct->response.data) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (value[1].len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "region \"%V\" in \"%V\" directive is invalid",
                           &value[1], &value[0]);
        return NGX_CONF_ERROR;
    }

    hrct->response = value[1];

    n = ngx_http_script_variables_count(&hrct->response);

    if (n == 0) {
        return NGX_CONF_OK;
    }

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

    sc.cf = cf;
    sc.source = &hrct->response;
    sc.lengths = &hrct->response_lengths;
    sc.values = &hrct->response_values;
    sc.variables = n;
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
