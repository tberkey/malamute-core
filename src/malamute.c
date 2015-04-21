/*  =========================================================================
    malamute - command-line service

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of the Malamute Project

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

/*
@header
    The Malamute Protocol is a persistent pub-sub protocol along with other
    things. To be expanded.
@discuss
@end
*/

#include "mlm_classes.h"

#define PRODUCT         "Malamute service/0.0.1"
#define COPYRIGHT       "Copyright (c) 2014 the Contributors"
#define NOWARRANTY \
"This Software is provided under the MPLv2 License on an \"as is\" basis,\n" \
"without warranty of any kind, either expressed, implied, or statutory.\n"

int main (int argc, char *argv [])
{
    puts (PRODUCT);
    puts (COPYRIGHT);
    puts (NOWARRANTY);

    int argn = 1;
    bool verbose = false;
    bool force_foreground = false;
    if (argc > argn && streq (argv [argn], "-v")) {
        verbose = true;
        argn++;
    }
    if (argc > argn && streq (argv [argn], "-f")) {
        force_foreground = true;
        argn++;
    }
    if (argc > argn && streq (argv [argn], "-h")) {
        puts ("Usage: malamute [ -v ] [ -f ] [ -h | config-file ]");
        puts ("  Default config-file is 'malamute.cfg'");
        return 0;
    }
    //  Collect configuration file name
    const char *config_file = "malamute.cfg";
    if (argc > argn) {
        config_file = argv [argn];
        argn++;
    }
    //  Send logging to system facility as well as stdout
    zsys_init ();
    zsys_set_logsystem (true);
    zsys_set_pipehwm (0);
    zsys_set_sndhwm (0);
    zsys_set_rcvhwm (0);
    
    //  Load config file for our own use here
    zsys_info ("starting Malamute using config in '%s'", config_file);
    zconfig_t *config = zconfig_load (config_file);
    if (config) {
        //  Do we want to run broker in the background?
        int as_daemon = !force_foreground && atoi (zconfig_resolve (config, "server/background", "0"));
        const char *workdir = zconfig_resolve (config, "server/workdir", ".");
        if (as_daemon) {
            zsys_info ("switching Malamute to background...");
            if (zsys_daemonize (workdir))
                return -1;
        }
        //  Switch to user/group to run process under, if any
        if (zsys_run_as (
            zconfig_resolve (config, "server/lockfile", NULL),
            zconfig_resolve (config, "server/group", NULL),
            zconfig_resolve (config, "server/user", NULL)))
            return -1;
    }
    else {
        zsys_error ("cannot load config file '%s'\n", config_file);
        return 1;
    }
    //  Install authenticator (NULL or PLAIN)
    zactor_t *auth = zactor_new (zauth, NULL);
    assert (auth);

    if (verbose || atoi (zconfig_resolve (config, "server/auth/verbose", "0"))) {
        zstr_sendx (auth, "VERBOSE", NULL);
        zsock_wait (auth);
    }
    //  Do PLAIN password authentication if requested
    const char *passwords = zconfig_resolve (config, "server/auth/plain", NULL);
    if (passwords) {
        zstr_sendx (auth, "PLAIN", passwords, NULL);
        zsock_wait (auth);
    }
    //  Start Malamute server instance
    zactor_t *server = zactor_new (mlm_server, "Malamute");
    if (verbose)
        zstr_send (server, "VERBOSE");
    zstr_sendx (server, "LOAD", config_file, NULL);

    //  Accept and print any message back from server
    while (true) {
        char *message = zstr_recv (server);
        if (message) {
            puts (message);
            free (message);
        }
        else {
            puts ("interrupted");
            break;
        }
    }
    //  Shutdown all services
    zactor_destroy (&server);
    zactor_destroy (&auth);
    
    //  Destroy config tree
    zconfig_destroy (&config);
    return 0;
}
