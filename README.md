# Shell Project

A UNIX-style shell written in C for an academic project.

## Features

- Command execution using `fork()` and `execvp()`
- Support for:
  - Pipes (`|`)
  - I/O redirection (`>`, `2>`)
  - Background processes (`&`)
  - Resource limits (`rlimit`) for CPU, memory, file size
- Execution time logging and statistics
- Blocking dangerous commands using an external `dangerous.txt` file

## Files

- `shell.c`: Main source code
- `Makefile`: For easy compilation
- `dangerous.txt`: List of blocked commands
- `exec_times.log`: Output log with execution stats

## How to Run

```bash
gcc -o shell shell.c -Wall
./shell dangerous.txt exec_times.log
# Run a background task
sleep 5 &

# Use a pipe
ls | grep .c

# Redirect errors
gcc shell.c 2> error.log
