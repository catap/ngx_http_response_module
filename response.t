#!/usr/bin/perl

# (C) Kirill A. Korinskiy

# Tests for response module.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib '../../tests/lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(4)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

master_process off;
daemon         off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location / {
            response "test-response";
            response_type "type/test";
        }


        location /default {
            default_type "default/type";
            response "default-type-response";
        }
    }
}

EOF

my $d = $t->testdir();

$t->run();

###############################################################################

my $r = http_get('/');

like($r, qr!^Content-Type: type/test!m, 'response_type type/test');
like($r, qr!^test-response!ms, 'response test-response');

$r = http_get('/default');

like($r, qr!^Content-Type: default/type!m, 'default_type default/type');
like($r, qr!^default-type-response!ms, 'response default-type-response');

###############################################################################
