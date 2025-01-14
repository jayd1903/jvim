#include "editor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "colors.h"
#include "command.h"
#include "layouts.h"
#include "search.h"
#include "text.h"
#include "utils.h"

EditorConfig *Editor_FreshSetup(char *filename) {
  EditorConfig *ec = (EditorConfig *)malloc(sizeof(EditorConfig));
  ec->file = TextFile_Setup(filename);
  ec->window_cursor.x = 0;
  ec->window_cursor.y = 1;
  ec->file_cursor.x = 0;
  ec->file_cursor.y = 1;
  ec->cmd_buf = CommandBuffer_Init();
  ec->find_replace = malloc(sizeof(FindReplace));
  ec->find_replace->find = NULL;
  ec->find_replace->replace = NULL;
  ec->find_replace->search_results = NULL;
  ec->find_replace->current_result = NULL;
  ec->mode = NORMAL;
  ec->running = true;
  return ec;
}

void Editor_ProcessKey(EditorConfig *config, char c) {
  // check if we are moving the cursor
  if (c == 27) {
    Editor_ProcessEscape(config);
    return;
  }
  // check if we are in insert mode
  if (config->mode == INSERT) {
    if (c == 127 || c == 8) {
      TextPos file_pos = config->window_cursor;
      file_pos.x += config->file_cursor.x;
      file_pos.y += config->file_cursor.y - 1;
      TextPos new_pos = TextFile_DeleteChar(config->file, file_pos);
      new_pos.x -= config->file_cursor.x;
      new_pos.y -= config->file_cursor.y - 1;
      Editor_SetCursor(config, new_pos);
    } else {
      TextPos file_pos = config->window_cursor;
      file_pos.x += config->file_cursor.x;
      file_pos.y += config->file_cursor.y - 1;
      TextPos new_pos = TextFile_InsertChar(config->file, c, file_pos);
      new_pos.x -= config->file_cursor.x;
      new_pos.y -= config->file_cursor.y - 1;
      Editor_SetCursor(config, new_pos);
    }
    Editor_Print(config);
  } else if (config->mode == COMMAND) {
    if (c == 127) {
      CommandBuffer_DeleteChar(config->cmd_buf);
    } else if (c == 10) {
      Editor_ProcessCommand(config);
      CommandBuffer_Clear(config->cmd_buf);
      // config->mode = NORMAL; TODO: uncomment this???
    } else {
      CommandBuffer_AddChar(config->cmd_buf, c);
    }
    Editor_PrintHeader(config);
  } else if (config->mode == NORMAL) {
    // if the user pressed the i key, enter insert mode
    if (c == 'i') {
      config->mode = INSERT;
      Editor_PrintHeader(config);
    } else if (c == '0') {
      config->window_cursor.x = 0;
    } else if (c == '$') {
      config->window_cursor.x =
          config->file
              ->lines[config->window_cursor.y + config->file_cursor.y - 2]
              ->line_length;
    } else if (c == 'G') {
      // move file_cursor to end of file, then move window cursor too
      config->file_cursor.y =
          config->file->num_lines - config->window_size.ws_row + 2;
      config->window_cursor.y = config->window_size.ws_row - 1;
      Editor_Print(config);
    } else if (c == 'o') {
      TextFile_InsertLine(config->file,
                          config->window_cursor.y + config->file_cursor.y - 1);
      TextPos new_pos = {.x = 0, .y = config->window_cursor.y + 1};
      Editor_SetCursor(config, new_pos);
      config->mode = INSERT;
      Editor_Print(config);
    } else if (c == 'O') {
      TextFile_InsertLine(config->file,
                          config->window_cursor.y + config->file_cursor.y - 2);
      config->window_cursor.x = 0;
      Editor_Print(config);
    } else if (c == ':') {
      config->mode = COMMAND;
      Editor_PrintHeader(config);
    }
  } else if (config->mode == FIND_REPLACE) {
    if (config->find_replace->current_result == NULL) {
      return;
    }
    if (c == 'n') {
      if (config->find_replace->current_result->next != NULL) {
        config->find_replace->current_result =
            config->find_replace->current_result->next;
      } else {
        config->find_replace->current_result =
            config->find_replace->search_results;
      }

      Editor_CursorToCurrentResult(config);
      Editor_Print(config);
    } else if (c == 10) {
      if (config->find_replace->replace == NULL) return;
      Search_ReplaceNextResult(config);
      if (config->find_replace->current_result == NULL) {
        config->mode = NORMAL;
      } else {
        Editor_CursorToCurrentResult(config);
      }
      Editor_Print(config);

    } else if (c == 'a') {
      if (config->find_replace->replace == NULL) return;
      Search_ReplaceAll(config);
      config->mode = NORMAL;
      Editor_Print(config);
    }

    else if (c == ':') {
      config->mode = COMMAND;
      Editor_Print(config);
    }
  }
}

void Editor_ProcessEscape(EditorConfig *config) {
  char c;
  int prev_mode = config->mode;
  // if the user just pressed the escape key, return to normal mode
  config->mode = NORMAL;
  CommandBuffer_Clear(config->cmd_buf);
  if (prev_mode == FIND_REPLACE) {
    Editor_Print(config);
  } else {
    Editor_PrintHeader(config);
  }
  if (read(STDIN_FILENO, &c, 1) == 1) {
    if (c == '[') {
      config->mode = prev_mode;
      if (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 'A') {
          Editor_MoveCursor(config, -1, 0);
        } else if (c == 'B') {
          Editor_MoveCursor(config, 1, 0);
        } else if (c == 'C') {
          Editor_MoveCursor(config, 0, 1);
        } else if (c == 'D') {
          Editor_MoveCursor(config, 0, -1);
        }

        Editor_Print(config);
      }
    } else {
      Editor_ProcessKey(config, c);
    }
  }
}

void Editor_ProcessCommand(EditorConfig *config) {
  config->cmd_buf->buf[config->cmd_buf->idx] = '\0';
  if (strcmp(config->cmd_buf->buf, "q!") == 0) {
    config->running = false;
  } else if (strcmp(config->cmd_buf->buf, "wq") == 0) {
    TextFile_Save(config->file);
    config->running = false;
  } else if (strcmp(config->cmd_buf->buf, "w") == 0) {
    TextFile_Save(config->file);
  } else if (startsWith("find ", config->cmd_buf->buf)) {
    config->mode = FIND_REPLACE;
    config->find_replace->find = config->cmd_buf->buf + 5;
    Search_Find(config);
    // move the cursor to the first result
    if (config->find_replace->current_result == NULL) {
      config->mode = NORMAL;
    } else {
      Editor_CursorToCurrentResult(config);
    }
    Editor_Print(config);
  } else if (startsWith("replace ", config->cmd_buf->buf)) {
    config->mode = FIND_REPLACE;
    // split by space to find the find and replace strings
    char *find = strtok(config->cmd_buf->buf + 8, " ");
    char *replace = strtok(NULL, " ");
    config->find_replace->find = strdup(find);
    config->find_replace->replace = strdup(replace);
    Search_Find(config);
    // move the cursor to the first result
    if (config->find_replace->current_result == NULL) {
      config->mode = NORMAL;
    } else {
      Editor_CursorToCurrentResult(config);
    }
    Editor_Print(config);
  }
  CommandBuffer_Clear(config->cmd_buf);
}

void Editor_MoveCursor(EditorConfig *config, int row_change, int col_change) {
  TextPos new_pos = {.x = config->window_cursor.x + col_change,
                     .y = config->window_cursor.y + row_change};
  Editor_SetCursor(config, new_pos);
}

void Editor_CursorToCurrentResult(EditorConfig *config) {
  if (config->find_replace->current_result == NULL) return;

  if (config->find_replace->current_result->range.start.y >
          config->file_cursor.y + config->window_size.ws_row - 3 ||
      config->find_replace->current_result->range.start.y <
          config->file_cursor.y) {
    config->file_cursor.y =
        config->find_replace->current_result->range.start.y + 1;
  }
  config->window_cursor.y =
      config->find_replace->current_result->range.start.y -
      config->file_cursor.y + 2;
  config->window_cursor.x = config->find_replace->current_result->range.start.x;
}

// TODO: fix this cursed method
void Editor_SetCursor(EditorConfig *config, TextPos pos) {
  config->window_cursor.x = pos.x;
  config->window_cursor.y = pos.y;
  if (config->window_cursor.x < 0) {
    if (config->window_cursor.y + config->file_cursor.y > 2) {
      config->window_cursor.y -= 1;
      config->window_cursor.x =
          config->file
              ->lines[config->window_cursor.y + config->file_cursor.y - 2]
              ->line_length;
    } else {
      config->window_cursor.x = 0;
    }
  }
  if (config->window_cursor.y < 1) {
    config->file_cursor.y -= 1;
    if (config->file_cursor.y < 1) {
      config->file_cursor.y = 1;
    }
    config->window_cursor.y = 1;
  }

  // don't allow the cursor to go past the end of the file
  if (config->window_cursor.y >
      config->file->num_lines - config->file_cursor.y + 1) {
    config->window_cursor.y =
        config->file->num_lines - config->file_cursor.y + 1;
  }

  if (config->window_cursor.y > config->window_size.ws_row - 1) {
    config->window_cursor.y = config->window_size.ws_row - 1;
    config->file_cursor.y += 1;
    if (config->file_cursor.y >
        config->file->num_lines - config->window_size.ws_row + 2) {
      config->file_cursor.y =
          config->file->num_lines - config->window_size.ws_row + 2;
    }
    config->window_cursor.y = config->window_size.ws_row - 1;
  }
  // don't allow the cursor to go past the end of the line
  if (config->window_cursor.x >
      config->file->lines[config->window_cursor.y + config->file_cursor.y - 2]
          ->line_length) {
    config->window_cursor.x =
        config->file->lines[config->window_cursor.y + config->file_cursor.y - 2]
            ->line_length;
  }
}

void Editor_PrintCursor(EditorConfig *config) {
  printf("\e[%d;%dH", config->window_cursor.y, config->window_cursor.x + 5);
}

void Editor_PrintHeader(EditorConfig *config) {
  TextPos old_pos = {.x = config->window_cursor.x,
                     .y = config->window_cursor.y};
  // make the cursor go to the bottom of the screen, based on
  // config->window_size.ws_row
  printf("\e[%d;1H", config->window_size.ws_row);
  // clear the current line
  printf("\33[2K\r");
  if (config->mode == INSERT) {
    printf(YELLOW);
    printf("  -- INSERT --");
    printf(RESETCOLOR);
  } else if (config->mode == COMMAND) {
    printf(BOLD);
    printf(":");
    printf("%.*s", config->cmd_buf->idx, config->cmd_buf->buf);
    printf(RESETFORMAT);
  } else if (config->mode == FIND_REPLACE) {
    printf(BOLD);
    printf(CYAN);
    printf("\"%s\"", config->find_replace->find);
    printf(RESETCOLOR);
    printf(RESETFORMAT);
  }
  Editor_SetCursor(config, old_pos);
  Editor_PrintCursor(config);
}

void Editor_Print(EditorConfig *config) {
  printf("\e[1;1H\e[2J");
  for (int i = config->file_cursor.y - 1;
       i < config->file_cursor.y + config->window_size.ws_row - 2; i++) {
    if (i < config->file->num_lines) {
      if (config->mode == FIND_REPLACE) {
        TextFile_PrintLine_SearchMode(config->file, i + 1,
                                      config->find_replace->search_results,
                                      config->find_replace->current_result);
        printf("\n");
      } else {
        TextFile_PrintLine(config->file, i + 1);
        printf("\n");
      }
    } else {
      printf("~\n");
    }
  }
  Editor_PrintHeader(config);
  Editor_PrintCursor(config);
}

void Editor_Free(EditorConfig *config) {
  TextFile_Free(config->file);
  free(config->find_replace);
  free(config);
}
