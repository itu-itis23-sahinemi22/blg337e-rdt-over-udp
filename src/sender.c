/* Sender: splits the file into packets and runs SW / GBN / SR (#6, #11, #12, #13). */
#include "cli.h"

int main(int argc, char **argv)
{
    struct config cfg;

    switch (cli_parse(argc, argv, ROLE_SENDER, &cfg)) {
    case CLI_HELP:  return 0;
    case CLI_ERROR: return 2;
    case CLI_OK:    break;
    }

    /* the transfer comes in the next steps of #6 */
    return 0;
}
