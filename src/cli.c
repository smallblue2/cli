#include <stdio.h>
#include <allocators/arena.h>
#include <string.h>

void create_command() { printf("MEOW\n"); }

typedef struct cmd cmd_t;

typedef enum { ACTION, GROUP } CMD_TYPE;

typedef struct {
  char **options;
  char **args;
  void *action;
} cmd_action_t;

typedef struct {
  cmd_t *children; // null terminated list
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

static alctr_arena_t *__CLI_TREE_ARENA;

int cli_print(cmd_t *cmd) {
  printf("%s: %s\n", cmd->name, cmd->desc);
  if (cmd->type == GROUP) {
    for (cmd_t *cur_cmd = cmd->group->children; cur_cmd != NULL; cur_cmd++) {
      printf("\t- %s: %s\n", cur_cmd->name, cur_cmd->desc);
    }
  } else {
    printf("\tOPTIONS:\n");
    for (char **option = cmd->action->options; option != NULL; option++) {
      printf("\t\t%s\n", *option);
    }
    printf("\tFLAGS:\n");
    for (char **flag = cmd->action->args; flag != NULL; flag++) {
      printf("\t\t%s\n", *flag);
    }
  }
  return 0;
}

cmd_t *add_to_group(cmd_t *child, cmd_t *parent) {
  
}

cmd_t *create_group(const char *name) {
  cmd_t *new_cmd = (cmd_t*)arena_alloc(__CLI_TREE_ARENA, sizeof(cmd_t));
  if (new_cmd == NULL) return NULL;
  new_cmd->type = GROUP;
  new_cmd->name = name;
  new_cmd->group = (cmd_group_t*)arena_alloc(__CLI_TREE_ARENA, sizeof(cmd_group_t));
  if (new_cmd->group == NULL) return NULL;
  new_cmd->group->children = NULL;
  return new_cmd;
}

// Can return NULL if the root fails to allocate on arena
cmd_t *cli_init(char **argv) {
  __CLI_TREE_ARENA = arena_create(1024);
  return create_group(*argv);
}

void cli_cleanup() {
  arena_free(__CLI_TREE_ARENA);
}

int cli_exec(cmd_t *root, int argc, char **argv) {
  // We're at our destination
  if (argc == 1) {
    printf("GOT TO '%s', SHOULD DO ITS ACTION\n", root->name);
    return 0;
  }

  // Figure out the next child to go to
  if (root->type != GROUP) {
    // Ideally print usage of the action
    printf("USAGE: %s\n", root->name);
    return 2;
  }

  // Traverse through current group's children until we're empty, or we find a match
  for (cmd_t *child = root->group->children; child != NULL && strcmp(child->name, *argv) != 0; child++) {
    return cli_exec(child, argc - 1, argv + 1);
  }
  
  printf("'%s' NOT IN GROUP '%s'\n", *argv, root->name);
  return 2;
}

int main(int argc, char **argv) {
  cmd_t *root = cli_init(argv);

  cli_exec(root, argc, argv);
  cli_cleanup();
  return 0;
}
