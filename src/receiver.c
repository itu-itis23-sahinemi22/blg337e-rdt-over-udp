/* Receiver: validates packets, sends ACKs and rebuilds the file (#6, #11, #12). */
#include "cli.h"

int main(int argc, char **argv)
{
    struct config cfg;

    switch (cli_parse(argc, argv, ROLE_RECEIVER, &cfg)) {
    case CLI_HELP:  return 0;
    case CLI_ERROR: return 2;
    case CLI_OK:    break;
    }

    /* the transfer comes in the next steps of #6 */
    return 0;
}
