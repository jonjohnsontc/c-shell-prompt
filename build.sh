# Lightweight script to compile the c-shell-prompt
cc -Wall -Werror -Wpedantic -std=c99 -o set-prompt ./my-shell-prompt.c