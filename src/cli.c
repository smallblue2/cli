#include "parray.h"
#include <stdio.h>
#include <collections/arena.h>
#include <collections/vector.h>
#include <string.h>
#include <stdbool.h>

void create_command() { printf("MEOW\n"); }

typedef struct cmd cmd_t;

typedef enum { ACTION, GROUP } CMD_TYPE;

typedef struct {
  c_vector_t *options;
  c_vector_t *flags;
  void *action;
} cmd_action_t;

typedef struct {
  c_vector_t *children;
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
static c_parray_t *__CLI_VECTOR_TRACKER;

int cli_print(cmd_t *cmd) {
  printf("%s: %s\n", cmd->name, cmd->desc);
  if (cmd->type == GROUP) {
    c_vector_t *children = cmd->group->children;
    for (int i = 0; i < vector_size(children); i++) {
      cmd_t *child;
      int ok = vector_get(children, i, &child);
      if (ok != 0) {
        printf("\t- NULL: NULL\n");
      } else {
        printf("\t- %s: %s\n", child->name, child->desc);
      }
    }
  } else {
    printf("\tOPTIONS:\n");
    c_vector_t *options = cmd->action->options;
    for (int i = 0; i < vector_size(options); i++) {
      char *opt;
      int ok = vector_get(options, i, &opt);
      if (ok != 0) {
        printf("\t\tNULL\n");
      } else {
        printf("\t\t%s\n", opt);
      }
    }
    printf("\tFLAGS:\n");
    c_vector_t *flags = cmd->action->flags;
    for (int i = 0; i < vector_size(flags); i++) {
      char *flag;
      int ok = vector_get(flags, i, &flag);
      if (ok != 0) {
        printf("\t\tNULL\n");
      } else {
        printf("\t\t%s\n", flag);
      }
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
  new_cmd->group->children = vector_create(sizeof(cmd_t*));

  // Add vector to global parray for easy cleanup
  parray_append(__CLI_VECTOR_TRACKER, &new_cmd->group->children, sizeof(c_vector_t*));

  return new_cmd;
}

// Returns false on error, true of success
bool add_to_group(cmd_t *parent, cmd_t *child) {
  if (parent->type != GROUP) {
    return false;
  }
  vector_push_back(parent->group->children, &child);
  return true;
}

// Can return NULL if the root fails to allocate on arena
cmd_t *cli_init(char **argv, const char *desc) {
  __CLI_TREE_ARENA = arena_create(1024);
  if (__CLI_TREE_ARENA == NULL) exit(1);
  __CLI_VECTOR_TRACKER = parray_create();
  if (__CLI_VECTOR_TRACKER == NULL) exit(1);
  return create_group(*argv, desc);
}

void cli_cleanup() {
  // Free each vector stored
  for (int i = 0; i < parray_length(__CLI_VECTOR_TRACKER); i++) {
    // Pop everything out to take ownership of it again
    // As we need to custom free it
    c_vector_t *vec = (c_vector_t*)parray_pop(__CLI_VECTOR_TRACKER, 0);
    if (vec == NULL) exit(1);
    vector_free(vec);
  }
  // Free the parray itself
  parray_free(__CLI_VECTOR_TRACKER);
  // Free all associated used memory
  arena_free(__CLI_TREE_ARENA);
}

typedef struct {
  cmd_t *last_cmd_node;
  c_vector_t *flags;
  c_vector_t *options;
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
    c_vector_t *children = root->group->children;
    for (int i = 0; i < vector_size(children); i++) {
      cmd_t *child;
      int ok = vector_get(children, i, &child);
      if (ok != 0) exit(1);
      // If we find a match, go to it
      if (strncmp(child->name, cur_str, sizeof(char) * strlen(cur_str)) == 0) {
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
    if (strncmp(argv[ctx->consumed], "--", 2) == 0) {
      ctx->options_only = true;
    } else {
      // Consume all hyphens - just want to store the flag name
      if (*(cur_str + 1) == '-') cur_str += 2;
      else cur_str++;
      // TODO: MAKE SURE IT'S NOT EMPTY
      int ok = vector_push_back(ctx->flags, &cur_str);
      if (ok != 0) {
        return -1;
      }
    }
    ctx->consumed++;
    return __cli_exec_ctx(root, argc, argv, ctx);
  }

  // Must be an option
add_option:
  int ok = vector_push_back(ctx->options, cur_str);
  if (ok != 0) {
    return -1;
  }
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
  ctx->flags = vector_create(sizeof(char**));
  if (ctx->flags == NULL) return -1;
  ctx->options = vector_create(sizeof(char**));
  if (ctx->options == NULL) {
    return -1;
  }

  // Add to global parray tracker for easy cleanup
  if (parray_append(__CLI_VECTOR_TRACKER, &ctx->flags, sizeof(c_vector_t*)) == -1) return -1;
  if (parray_append(__CLI_VECTOR_TRACKER, &ctx->options, sizeof(c_vector_t*)) == -1) return -1;

  return __cli_exec_ctx(root, argc, argv, ctx);
}

int main(int argc, char **argv) {
  cmd_t *root = cli_init(argv, "A test program");

  cmd_t *meow = create_group("meow", "print a cat!");
  add_to_group(root, meow);

  cli_exec(root, argc, argv);

  cli_cleanup();
  return 0;
}
