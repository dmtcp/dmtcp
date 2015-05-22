# DMTCP plugin for checkpoint timer

Use this plugin to display a warning or kill an application if checkpointing
takes too long.

Following environment variables can be used to customize the behavior of
the plugin:

    DMTCP_CKPTTIMER_SIGNAL:   Use this to specify the timeout signal
    DMTCP_CKPTTIMER_ACTION:   Use this to specify the action on timeout
    DMTCP_CKPTTIMER_INTERVAL: Use this to specify the timeout interval
                              in seconds

Example:

    $ dmtcp_launch --with-plugin /path/to/dmtcp_ckpttimerhijack.so <your-app>
