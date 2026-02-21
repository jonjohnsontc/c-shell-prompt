#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Hello! I display the following in separate lines:
// Current python3 env 
// Current Git branch
// Current AWS environment (based on a configured environment)
// file-path to root of git directory
// User @ Host in current working directory
// Final prompt char

#define BUF_SIZE				4096
#define MAX_LINE_SIZE			1024
#define PREFIX_SIZE				4
#define PATH_SEP				'/'

#ifdef __APPLE__
#define HOST                    "mac"
#elifdef __WIN32
#define HOST                    "win"
#elifdef __linux__
#define HOST                    "linux"
#else
#define HOST                    "unknown"
#endif

// Style specific constants
#define PREFIX					"▒ "
#define PROMPT_CHAR				"℁"

// Colors
#define RESET					"\033[0m"
#define RED						"\033[31m"
#define GREEN					"\033[32m"
#define YELLOW					"\033[33m"
#define BLUE					"\033[34m"
#define MAGENTA					"\033[35m"
#define CYAN					"\033[36m"
#define WHITE					"\033[37m"
#define BOLD_MAGENTA			"\033[1;35m"
#define DEFAULT_COLOR           BOLD_MAGENTA            // Change this color to update the UI
#define COLORIZE(color, text)   color text RESET

// Bad ideas
#define CONCAT_3(thing1, thing2, thing3)  thing1 thing2 thing3

typedef struct
{
    size_t cur;
    char val[BUF_SIZE];
} StringBuffer;

// I'm using fwd declarations in this project. I want to keep everything
// to a single file, and also allow the user to read the main logic ASAP
bool is_small_enough(size_t str_size, size_t cur);
// Helper fn to add unterminated strings to the prompt
void sb_append(StringBuffer* buffer, const char* string, size_t len);
// Retrieves the index of the next directory character in a string
size_t find_next_dir(const char* path_string, size_t current_idx);
// Walk up directories looking for git information and add to prompt
void add_git_info(StringBuffer* buffer, const char* cwd);
void add_shell_info(StringBuffer* buffer);
void add_local_host_and_dir(StringBuffer* buffer, const char* cwd);
void add_python_env(StringBuffer* buffer);
void add_bottom_row(StringBuffer* buffer);
void add_prompt_char(StringBuffer* buffer);
void add_row_to_prompt(StringBuffer* prompt, const char* row, size_t row_size);
void add_color(StringBuffer* buffer, const char* color);
void add_long_cwd(StringBuffer* buffer, const char* cwd);
bool exists(DIR* directory, const char* name, uint8_t file_type);

int main(void)
{
    StringBuffer output = {0};
    add_python_env(&output);
    const char* cwd = getenv("PWD");
    if (cwd != NULL)
    {
        add_local_host_and_dir(&output, cwd);
        add_long_cwd(&output, cwd);
        add_git_info(&output, cwd);
    }
    add_shell_info(&output);
    add_bottom_row(&output);
    add_prompt_char(&output);

    printf("%s", output.val);
    return 0;
}

bool is_small_enough(size_t str_size, size_t cur)
{
    return (str_size < MAX_LINE_SIZE && cur + str_size < BUF_SIZE);
}

void sb_append(StringBuffer* buffer, const char* string, size_t len)
{
    constexpr size_t buffer_size = BUF_SIZE;
    if (buffer->cur + len < buffer_size)
    {
        memcpy(buffer->val + buffer->cur, string, len);
        buffer->cur += len;
        buffer->val[buffer->cur] = '\0';
    }
}

void add_row_to_prompt(StringBuffer* prompt, const char* row, size_t row_size)
{
    const char* prefix = COLORIZE(DEFAULT_COLOR, PREFIX);
    const size_t prefix_size = strnlen(prefix, MAX_LINE_SIZE);
    const size_t total_size = row_size + prefix_size;
    if (is_small_enough(total_size, prompt->cur))
    {
        constexpr char end_char = '\n';
        sb_append(prompt, prefix, prefix_size);
        sb_append(prompt, row, row_size);
        prompt->val[prompt->cur] = end_char;
        prompt->cur += 1;
    }
}

void add_long_cwd(StringBuffer* buffer, const char* cwd)
{
    size_t directory_cnt = 0;
    for (size_t i = strnlen(cwd, MAX_LINE_SIZE); i > 0; i = find_next_dir(cwd, i))
        directory_cnt++;

    if (directory_cnt >= 5)
    {
        StringBuffer row = {.val = "pwd: ", .cur = 5};
        const size_t cwd_start_idx = find_next_dir(cwd, strnlen(cwd, MAX_LINE_SIZE));
        sb_append(&row, &cwd[cwd_start_idx + 1], strnlen(&cwd[cwd_start_idx] + 1, MAX_LINE_SIZE));
        add_row_to_prompt(buffer, row.val, row.cur);
    }
}

void add_git_info(StringBuffer* buffer, const char* cwd)
{
    size_t cur = strnlen(cwd, MAX_LINE_SIZE);
    char dirname[MAX_LINE_SIZE];

    snprintf(dirname, sizeof(dirname), "%s", cwd);
    while (cur > 0)
    {
        DIR* dir = opendir(dirname);
        if (dir == NULL)
        {
            fprintf(stderr, "my-shell-prompt: could not open %s, %s\n", dirname, strerror(errno));
            return;
        }
        if (exists(dir, ".git", DT_DIR))
        {
            char git_dirname[MAX_LINE_SIZE];
            snprintf(git_dirname, sizeof(git_dirname), "%s/.git", dirname);
            DIR* git_dir = opendir(git_dirname);
            if (git_dir == NULL)
            {
                fprintf(stderr, "my-shell-prompt: could not open git directory %s, %s\n", git_dirname,
                        strerror(errno));
                return;
            }
            if (exists(git_dir, "HEAD", DT_REG))
            {
                // open file
                char git_head[MAX_LINE_SIZE];
                snprintf(git_head, sizeof(git_head), "%s/HEAD", git_dirname);
                const int fd = open(git_head, O_RDONLY | O_NONBLOCK);
                if (fd == -1)
                {
                    fprintf(stderr, "my-shell-prompt: open %s failed\n", git_head);
                    return;
                }

                // read to buffer
                char branch_info[MAX_LINE_SIZE];
                const ssize_t bytes_read = read(fd, (void*)branch_info, MAX_LINE_SIZE);
                if (bytes_read == -1)
                {
                    fprintf(stderr, "my-shell-prompt: read %s failed\n", git_head);
                    return;
                }
                branch_info[bytes_read] = '\0';

                // examine chars , determine if detached or not
                const char* std_prefix = "ref: refs/heads/";
                const size_t std_prefix_size = strnlen(std_prefix, MAX_LINE_SIZE);
                if (strncmp(branch_info, std_prefix, std_prefix_size) == 0)
                {
                    // process as branch
                    StringBuffer git_branch_info = {.val = "On branch: ", .cur = 11};
                    // Remove LF char from end of HEAD name
                    const size_t branch_size = bytes_read - std_prefix_size - 1;
                    for (unsigned i = 0; i < branch_size; i++)
                    {
                        git_branch_info.val[git_branch_info.cur] = branch_info[std_prefix_size + i];
                        git_branch_info.cur++;
                    }
                    add_row_to_prompt(buffer, git_branch_info.val, git_branch_info.cur);
                }
                else
                {
                    // process as detached SHA
                    char detached_sha[7];
                    snprintf(detached_sha, sizeof(detached_sha), "%s", branch_info);
                    add_row_to_prompt(buffer, detached_sha, 7);
                }
                closedir(git_dir);
                return;
            }
            else
            {
                fprintf(stderr, "my-shell-prompt: could not find HEAD in git directory: %s\n", git_dirname);
                return;
            }
        }
        if (closedir(dir) < 0)
        {
            fprintf(stderr, "my-shell-prompt: could not close %s: %s\n", dirname, strerror(errno));
            return;
        }
        const size_t next_dir = find_next_dir(dirname, cur);
        cur = next_dir;
        snprintf(dirname, next_dir+1, "%s", cwd);
    }
}

size_t find_next_dir(const char* path_string, size_t current_idx)
{
    if (current_idx == 0) return 0;
    size_t cur = current_idx - 1;
    while (cur > 0 && path_string[cur] != PATH_SEP) cur -= 1;
    return cur;
}

bool exists(DIR* directory, const char* name, uint8_t file_type)
{
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL)
    {
        if (entry->d_type == file_type && (strcmp(entry->d_name, name) == 0))
        {
            return true;
        }
    }
    return false;
}

void add_shell_info(StringBuffer* buffer)
{
    const char* shell = getenv("SHELL");
    if (shell != NULL)
    {
        size_t shell_env_var_size = strnlen(shell, MAX_LINE_SIZE);
        // TODO: I'm assuming that the shell which is being used is accessible
        // from /bin/
        size_t start_idx = 5;
        StringBuffer shell_info = {.val = "Using ", .cur = 6};
        for (unsigned i = start_idx; i < shell_env_var_size; i++)
        {
            shell_info.val[shell_info.cur] = shell[i];
            shell_info.cur++;
        }
        add_row_to_prompt(buffer, shell_info.val, shell_info.cur);
    }
}

void add_bottom_row(StringBuffer* buffer)
{
    const char* bottom_row = COLORIZE(DEFAULT_COLOR, "▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔");
    sb_append(buffer, bottom_row, strlen(bottom_row));
}

void add_prompt_char(StringBuffer* buffer)
{
    const char* prompt_chars = CONCAT_3("\n", PROMPT_CHAR, "\n");
    sb_append(buffer, prompt_chars, strlen(prompt_chars));
}

void add_python_env(StringBuffer* buffer)
{
    const char* pyenv = getenv("VIRTUAL_ENV");
    if (pyenv != NULL)
    {
        StringBuffer env = {0};
        const size_t env_name_size = strnlen(pyenv, MAX_LINE_SIZE);
        const size_t next_idx = find_next_dir(pyenv, env_name_size);
        sb_append(&env, "Py env: ", strlen("Py env: "));
        sb_append(&env, &pyenv[next_idx + 1], env_name_size - next_idx - 1);
        add_row_to_prompt(buffer, env.val, env.cur);
    }
}

void add_local_host_and_dir(StringBuffer* buffer, const char* cwd)
{
    const char* localhost = CONCAT_3("JJ@", HOST, " in ");
    StringBuffer row = {0};
    sb_append(&row, localhost, strnlen(localhost, MAX_LINE_SIZE));
    size_t cwd_start_idx = find_next_dir(cwd, strnlen(cwd, MAX_LINE_SIZE));
    sb_append(&row, &cwd[cwd_start_idx + 1], strnlen(&cwd[cwd_start_idx + 1], MAX_LINE_SIZE));
    add_row_to_prompt(buffer, row.val, row.cur);
}