/**
 * @file node_config_check.c
 * @brief PC program that validates a config file with the firmware's parser.
 *
 * Usage: node_config_check FILE
 *
 * Runs node_config_parse() (spec 03) on FILE, so a config is checked against
 * exactly the rules the node applies at boot. Warnings are printed to stderr
 * as "warning: <path>: <message>"; a rejected config is printed to stderr as
 * "<path>: <message>". See NODE_CONFIG_CHECK_EXIT_* for the exit codes.
 */

/* ==========================================================================
 * INCLUDES
 * ========================================================================== */

#include <stdio.h>

#include "node_config.h"

/* ==========================================================================
 * MACROS
 * ========================================================================== */

/** @brief Exit code: the parser accepted the config. */
#define NODE_CONFIG_CHECK_EXIT_VALID 0

/** @brief Exit code: the parser rejected the config. */
#define NODE_CONFIG_CHECK_EXIT_INVALID 1

/** @brief Exit code: wrong arguments, or the file could not be read. */
#define NODE_CONFIG_CHECK_EXIT_USAGE 2

/** @brief Size of the file buffer: one byte over the parser's limit, so an
 *         oversized file reaches the parser and is rejected with its own
 *         message instead of being cut short silently. */
#define NODE_CONFIG_CHECK_BUF_LEN (NODE_CONFIG_MAX_RAW_LEN + 1U)

/* ==========================================================================
 * GLOBAL VARIABLES
 * ========================================================================== */

/** @brief Contents of the config file; written only by
 *         node_config_check_read_file(). */
static char g_raw_config[NODE_CONFIG_CHECK_BUF_LEN] = {0};

/** @brief Parsed config; filled by node_config_parse() and otherwise unused.
 *         Static to keep it off the stack. */
static node_config_t g_config = {0};

/* ==========================================================================
 * PRIVATE FUNCTION PROTOTYPES
 * ========================================================================== */

/**
 * @brief Read up to NODE_CONFIG_CHECK_BUF_LEN bytes of a file into
 *        g_raw_config.
 *
 * @param path    File to read; must not be NULL.
 * @param out_len Receives the number of bytes read; must not be NULL.
 * @return 0 on success, -1 if the file cannot be opened or read. The reason
 *         has then been printed to stderr.
 */
static int node_config_check_read_file(const char *path, size_t *out_len);

/* ==========================================================================
 * PUBLIC FUNCTIONS
 * ========================================================================== */

int main(int argc, char **argv) {
  if (argc != 2) {
    (void)fprintf(stderr, "usage: node_config_check FILE\n");
    return NODE_CONFIG_CHECK_EXIT_USAGE;
  }

  size_t len = 0;
  if (node_config_check_read_file(argv[1], &len) != 0) {
    return NODE_CONFIG_CHECK_EXIT_USAGE;
  }

  node_config_err_t err = {0};
  if (node_config_parse(g_raw_config, len, &g_config, &err) != NODE_CONFIG_OK) {
    (void)fprintf(stderr, "%s\n", err.msg);
    return NODE_CONFIG_CHECK_EXIT_INVALID;
  }
  return NODE_CONFIG_CHECK_EXIT_VALID;
}

/* ==========================================================================
 * PRIVATE FUNCTIONS
 * ========================================================================== */

static int node_config_check_read_file(const char *path, size_t *out_len) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    (void)fprintf(stderr, "%s: cannot open file\n", path);
    return -1;
  }

  size_t len = fread(g_raw_config, 1, sizeof(g_raw_config), file);
  int read_failed = ferror(file);
  /* The file was only read, so a failing close loses nothing. */
  (void)fclose(file);
  if (read_failed != 0) {
    (void)fprintf(stderr, "%s: cannot read file\n", path);
    return -1;
  }

  *out_len = len;
  return 0;
}
