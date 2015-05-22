# DMTCP plugin for Apache2

Use this plugin when running Apache2 with PHP-FastCGI.

Example:

    $ sudo dmtcp_launch --ckptdir /my/ckpts --tmpdir /my/tmp \
       --with-plugin /path/to/dmtcp_dmtcp_apachehijack.so httpd
