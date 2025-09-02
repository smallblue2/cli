#include <stdio.h>
#include <collections/arena.h>
#include <collections/parray.h>
#include <string.h>
#include <stdbool.h>

void create_command() { printf("MEOW\n"); }

typedef struct cmd cmd_t;

typedef enum { ACTION, GROUP } CMD_TYPE;

typedef struct {
  c_parray_t *options;
  c_parray_t *flags;
  void *action;
} cmd_action_t;

typedef struct {
  c_parray_t *children;
} cmd_group_t;

struct cmd {
  const char *name;
  const char *desc;
  int (*exec)(int,char**);
  CMD_TYPE type;
  union {
    cmd_action_t *action;
    cmd_group_t *group;
  };
};

static c_arena_t *__CLI_TREE_ARENA;
static c_parray_t *__CLI_PARRAY_TRACKER;

int cli_print(cmd_t *cmd) {
  printf("%s: %s\n", cmd->name, cmd->desc);
  if (cmd->type == GROUP) {
    c_parray_t *children = cmd->group->children;
    for (int i = 0; i < parray_length(children); i++) {
      cmd_t *child = (cmd_t*)parray_get(children, i);
      if (child == NULL) {
        printf("\t- NULL: NULL\n");
      }
      printf("\t- %s: %s\n", child->name, child->desc);
    }
  } else {
    printf("\tOPTIONS:\n");
    c_parray_t *options = cmd->action->options;
    for (int i = 0; i < parray_length(options); i++) {
      char *opt = (char*)parray_get(options, i);
      if (opt == NULL) {
        printf("\t\tNULL\n");
      }
      printf("\t\t%s\n", opt);
    }
    printf("\tFLAGS:\n");
    c_parray_t *flags = cmd->action->options;
    for (int i = 0; i < parray_length(flags); i++) {
      char *flag = (char*)parray_get(flags, i);
      if (flag == NULL) {
        printf("\t\tNULL\n");
      }
      printf("\t\t%s\n", flag);
    }
  }
  return 0;
}

cmd_t *create_group(const char *name, const char *desc) {
  cmd_t *new_cmd = (cmd_t*)arena_alloc(__CLI_TREE_ARENA, sizeof(cmd_t));
  if (new_cmd == NULL) return NULL;
  new_cmd->type = GROUP;
  new_cmd->name = name;
  new_cmd->desc = desc;
  new_cmd->group = (cmd_group_t*)arena_alloc(__CLI_TREE_ARENA, sizeof(cmd_group_t));
  if (new_cmd->group == NULL) return NULL;
  new_cmd->group->children = parray_create();

  // Add parray to global parray for easy cleanup

  return new_cmd;
}

// Returns false on error, true of success
bool add_to_group(cmd_t *parent, cmd_t *child) {
  if (parent->type != GROUP) {
    return false;
  }
  parray_append(parent->group->children, child, sizeof(cmd_t));
  return true;
}

// Can return NULL if the root fails to allocate on arena
cmd_t *cli_init(char **argv, const char *desc) {
  __CLI_TREE_ARENA = arena_create(1024);
  if (__CLI_TREE_ARENA == NULL) exit(1);
  __CLI_PARRAY_TRACKER = parray_create();
  if (__CLI_PARRAY_TRACKER == NULL) exit(1);
  return create_group(*argv, desc);
}

void cli_cleanup() {
  for (int i = 0; i < parray_length(__CLI_PARRAY_TRACKER); i++) {
    c_parray_t *parray = (c_parray_t*)parray_get(__CLI_PARRAY_TRACKER, i);
    if (parray == NULL) exit(1);
    parray_free(parray);
  }
  parray_free(__CLI_PARRAY_TRACKER);
  arena_free(__CLI_TREE_ARENA);
}

typedef struct {
  cmd_t *last_cmd_node;
  c_parray_t *flags;
  c_parray_t *options;
  bool options_only;
  int consumed;
} __cmd_ctx_t;

int __cli_exec_ctx(cmd_t *root, int argc, char **argv, __cmd_ctx_t *ctx) {
  // Are we at the end?
  if (ctx->consumed == argc) {
finished:
    if (root->type == GROUP) {
      printf("FINISHED! [GROUP]\n\n");
    } else {
      // Execute something
      printf("FINISHED! [ACTION]\n");
      root->action->options = ctx->options;
      root->action->flags = ctx->flags;
    }

    cli_print(root);
    return 0;
  }

  const char *cur_str = argv[ctx->consumed];

  // Are we in options only mode?
  if (ctx->options_only) {
    goto add_option;
  }

  // Are we at a group?
  if (root->type == GROUP) {
    // Are we recursing deeper?
    c_parray_t *children = root->group->children;
    for (int i = 0; i < parray_length(children); i++) {
      cmd_t *child = (cmd_t*)parray_get(children, i);
      if (child == NULL) exit(1);
      // If we find a match, go to it
      if (strncmp(child->name, cur_str, sizeof(char) * (strlen(cur_str) + 1)) == 0) {
        ctx->consumed++;
        return __cli_exec_ctx(child, argc, argv, ctx);
      }
    }
    // No match, ignore flags
    printf("USAGE: xxxxx\n");
    printf("FINISHED (shortcut)! [GROUP]\n"); // Print group info
    goto finished;
  }

  // Is the next item a flag?
  if (*cur_str == '-') {
    // If it is exactly "--", set the options only flag
    if (strncmp(argv[ctx->consumed], "--", 3)) {
      ctx->options_only = true;
    } else {
      // Consume all hyphens - just want to store the flag name
      if (*(cur_str + 1) == '-') cur_str += 2;
      else cur_str++;
      parray_append(ctx->flags, cur_str, sizeof(char) * (strlen(cur_str) + 1));
    }
    ctx->consumed++;
    return __cli_exec_ctx(root, argc, argv, ctx);
  }

  // Must be an option
add_option:
  parray_append(ctx->options, cur_str, sizeof(char) * (strlen(cur_str) + 1));
  ctx->consumed++;
  return __cli_exec_ctx(root, argc, argv, ctx);
}

int cli_exec(cmd_t *root, int argc, char **argv) {
  __cmd_ctx_t *ctx = arena_alloc(__CLI_TREE_ARENA, sizeof(__cmd_ctx_t));
  if (ctx == NULL) {
    return 1;
  }

  // Starts at 1 as we know the first argument must be the binary name
  ctx->consumed = 1;
  ctx->options_only = false;
  ctx->last_cmd_node = NULL;
  ctx->flags = parray_create();
  if (ctx->flags == NULL) return 1;
  ctx->options = parray_create();
  if (ctx->options == NULL) {
    parray_free(ctx->flags);
    return 1;
  }

  int result = __cli_exec_ctx(root, argc, argv, ctx);

  parray_free(ctx->flags);
  parray_free(ctx->options);

  return result;
}

int main(int argc, char **argv) {
  cmd_t *root = cli_init(argv, "A test program");
  cmd_t *meow = create_group("meow", "print a cat!");
  add_to_group(root, meow);
  cli_exec(root, argc, argv);
  cli_cleanup();
  return 0;
}
