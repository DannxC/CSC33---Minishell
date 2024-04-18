#include <stdio.h>     // For input/output functions
#include <stdlib.h>    // For general utilities like exit()
#include <string.h>    // For string manipulation functions
#include <ctype.h>     // For character class tests
#include <unistd.h>    // For POSIX operating system API, e.g., fork(), pipe(), ...
#include <sys/types.h> // For system data type definitions
#include <sys/wait.h>  // For waiting for process termination
#include <fcntl.h>     // For file control options
#include <stdbool.h>   // For boolean data type

#define MAX_LINE 1024  // Define a constant for maximum input line length
#define MAX_ARGS 32    // Maximum number of arguments per command
#define MAX_COMMANDS 8 // Max number of commands inside a pipeline

// Single command struct
typedef struct
{
    char *command;    // The command to execute
    char **args;      // Arguments list, null-terminated
    char *inputFile;  // Input redirection file, or NULL
    char *outputFile; // Output redirection file, or NULL
} Command;

// Structure to hold file descriptors for a pipe
typedef struct
{
    int read_fd;  // Read end of the pipe
    int write_fd; // Write end of the pipe
} PipeFD;

// Function to trim leading and trailing whitespace in place
void trim(char *str)
{
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str))
        str++;

    // If all spaces, str will point to '\0'
    if (*str == 0)
    {
        return;
    }

    // Find the end of the string
    end = str + strlen(str) - 1;

    // Trim trailing space
    while (end > str && isspace((unsigned char)*end))
        end--;

    // Write new null terminator
    *(end + 1) = '\0';
}

// Function to reset the command structure and free allocated resources
void reset_commands(Command *commands, int *numCommands)
{
    for (int i = 0; i < *numCommands; i++)
    {
        // Reset command and file redirection pointers
        commands[i].command = NULL;
        // Clear the args array if it's not NULL
        for (int j = 0; commands[i].args[j] != NULL; j++)
        {
            commands[i].args[j] = NULL;
        }
        commands[i].inputFile = NULL;
        commands[i].outputFile = NULL;
    }

    *numCommands = 0; // Reset the number of commands to 0
}

// Function to parse a single command segment (without '|') into a Command structure
void parse_single_command(char *segment, Command *command)
{
    // Initialize pointers and counters at the start
    command->command = NULL;
    command->inputFile = NULL;
    command->outputFile = NULL;
    int argCount = 0; // Keep track of the number of arguments

    // Tokenize the segment string using space as a delimiter
    char *token = strtok(segment, " ");
    command->command = token;          // The first token is the shell command itself
    command->args[argCount++] = token; // The first token is also the first argument

    // Iterate oiver the rest of the tokens to parse arguments and redirections
    while (token != NULL)
    {
        token = strtok(NULL, " "); // Move to the next token
        if (token == NULL)
            break; // Skip NULL tokens

        if (strcmp(token, "<") == 0)
        { // If token is input redirection symbol
            token = strtok(NULL, " ");
            command->inputFile = token; // Next token is the input file name
        }
        else if (strcmp(token, ">") == 0)
        { // If token is output redirection symbol
            token = strtok(NULL, " ");
            command->outputFile = token; // Next token is the output file name
        }
        else
        {
            // If it's not a redirection, consider it as part of the arguments
            if (argCount < MAX_ARGS - 1)
            { // Leave space for NULL terminator
                command->args[argCount++] = token;
            }
        }
    }

    command->args[argCount] = NULL; // NULL-terminate the arguments array
}

// Returns true if input is valid, false if there is an error
bool verify_input(char *input)
{
    size_t input_length = strlen(input);

    // Remove newline character at the end, if present
    if (input[input_length - 1] == '\n')
    {
        input[--input_length] = '\0'; // Adjust length accordingly
    }

    // Check if the input is empty or only whitespace
    if (input_length == 0 || strspn(input, " ") == input_length)
    {
        printf("Error: Input is empty or contains only spaces.\n");
        return false;
    }

    // Check for input length constraints
    if (input_length >= MAX_LINE)
    {
        printf("Error: Input too long.\n");
        return false;
    }

    // Check if the input starts or ends with a pipe
    if (input[0] == '|' || input[input_length - 1] == '|')
    {
        printf("Error: Input cannot start or end with a pipe.\n");
        return false;
    }

    // Check for two consecutive pipes or improperly placed spaces around pipes
    if (strstr(input, "||") != NULL)
    {
        printf("Error: Improper use of pipes.\n");
        return false;
    }

    // Verify if the input contains only pipes and/or spaces
    if (strspn(input, " |") == input_length)
    {
        printf("Error: Input contains only pipes and spaces.\n");
        return false;
    }

    // Count pipes to estimate number of commands
    int pipe_count = 0;
    for (int i = 0; i < input_length; i++)
    {
        if (input[i] == '|')
            pipe_count++;
    }
    if (pipe_count >= MAX_COMMANDS)
    {
        printf("Error: Too many commands.\n");
        return false;
    }

    return true; // Input is valid
}

// Function to parse user input into an array of Command structures
void parse_input(char *input, Command *commands, int *numCommands)
{
    *numCommands = 0;

    // Trim leading and trailing whitespace
    trim(input);

    // Only proceed if the input is valid
    if (!verify_input(input))
    {
        return;
    }

    char *segments[MAX_COMMANDS]; // Array to hold pointers to each command segment
    char *segment;                // Pointer to the current segment

    // First split by '|'
    segment = strtok(input, "|");
    while (segment != NULL && *numCommands < MAX_COMMANDS)
    {
        segments[(*numCommands)++] = segment; // Store the segment pointer
        segment = strtok(NULL, "|");          // Move to the next segment
    }

    // Now parse each segment into a Command structure
    for (int i = 0; i < *numCommands; i++)
    {
        parse_single_command(segments[i], &commands[i]);
    }
}

// Function to execute all commands in the pipeline
void execute_commands(Command *commands, int *numCommands)
{
    int i;

    // Allocate memory for the pipe descriptors
    PipeFD *pipes = malloc((*numCommands - 1) * sizeof(PipeFD));
    if (!pipes)
    {
        perror("Failed to allocate memory for pipe descriptors");
        exit(EXIT_FAILURE);
    }

    // Create the pipes
    int tmp_fds[2]; // Temporary file descriptors for pipe creation
    for (i = 0; i < *numCommands - 1; i++)
    {
        if (pipe(tmp_fds) < 0)
        {
            perror("Couldn't create a pipe");
            free(pipes);
            exit(EXIT_FAILURE);
        }
        pipes[i].read_fd = tmp_fds[0];
        pipes[i].write_fd = tmp_fds[1];
    }

    // Execute commands in a loop (concurrently)
    // Notice that even if, for example, cmd1 | cmd2 | cmd3 is the input, the loop will execute cmd1, cmd2, and cmd3 concurrently
    // But it is not a problem, because the pipes are set up correctly... the OS will handle data dependencies and if cmd2 executes before cmd1,
    // it will just wait for the data to be available in the pipe by cmd1
    for (i = 0; i < *numCommands; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        { // Child process

            /* HANDLE WITH PIPE REDIRECTION */

            // Close all pipe fds not used by this command
            for (int j = 0; j < *numCommands - 1; j++)
            {
                if (j != i - 1 && j != i)
                {
                    close(pipes[j].read_fd);
                    close(pipes[j].write_fd);
                }
            }

            // If not the first command, get input from the previous pipe
            if (i != 0)
            {
                close(pipes[i - 1].write_fd);             // Close the write end of the previous pipe
                dup2(pipes[i - 1].read_fd, STDIN_FILENO); // Duplicate the read end of the previous pipe to stdin (file descriptor = 0)
            }

            // If not the last command, set output to the next pipe
            if (i != *numCommands - 1)
            {
                close(pipes[i].read_fd);                // Close the read end of the next pipe
                dup2(pipes[i].write_fd, STDOUT_FILENO); // Duplicate the write end of the next pipe to stdout (file descriptor = 1)
            }

            /* HANDLE WITH INPUT/OUTPUT REDIRECTION */

            if (commands[i].inputFile != NULL)
            {
                FILE *fpIn = fopen(commands[i].inputFile, "r"); // Open file for reading
                if (fpIn == NULL)
                {
                    perror("Failed to open input file");
                    exit(EXIT_FAILURE); // Exit the child process if file opening fails
                }
                dup2(fileno(fpIn), STDIN_FILENO); // Duplicate file descriptor to stdin (file descriptor = 0)
                fclose(fpIn);                     // Close the file after duplicating the file descriptor (not needed anymore
            }

            if (commands[i].outputFile != NULL)
            {
                FILE *fpOut = fopen(commands[i].outputFile, "w"); // Open file for writing (this creates the file if it doesn't exist)
                if (fpOut == NULL)
                {
                    perror("Failed to open output file");
                    exit(EXIT_FAILURE); // Exit the child process if file creation fails
                }
                dup2(fileno(fpOut), STDOUT_FILENO); // Duplicate file descriptor to stdout (file descriptor = 1)
                fclose(fpOut);                      // Close the file after duplicating the file descriptor (not needed anymore)
            }

            /* EXECUTING THE COMMAND */

            char *envp[] = {NULL};
            if (execve(commands[i].command, commands[i].args, envp) == -1)
            {
                perror("execve failed");

                // If not the first command, stdin may be duplicated from a pipe.
                if (i != 0)
                {
                    close(STDIN_FILENO);
                }

                // If not the last command, stdout may be duplicated to a pipe.
                if (i != *numCommands - 1)
                {
                    close(STDOUT_FILENO);
                }

                // Exit the child process if execve fails
                exit(EXIT_FAILURE);
            }
        }
    }

    // Parent closes all pipe file descriptors
    for (int i = 0; i < *numCommands - 1; i++)
    {
        close(pipes[i].read_fd);
        close(pipes[i].write_fd);
    }

    // Free the dynamically allocated memory for pipes
    free(pipes);

    // Wait for all child processes
    while (wait(NULL) > 0)
        ;
}

int main()
{
    char input[MAX_LINE]; // Buffer for the user input

    // Initiate array of commands
    Command commands[MAX_COMMANDS];
    for (int i = 0; i < MAX_COMMANDS; i++)
    {
        commands[i].args = malloc(MAX_ARGS * sizeof(char *)); // Allocate memory for arguments
    }

    // Main loop to continuously accept user commands
    while (1)
    {
        printf("\033[0;32mcmd> \033[0m"); // Prompt the user for input

        // Read a line of input and exit the loop if EOF is encountered
        if (!fgets(input, MAX_LINE, stdin))
        {
            break;
        }

        // Remove the trailing newline character from the input
        input[strcspn(input, "\n")] = '\0';

        // Parse the input into commands
        int numCommands = 0;                        // Number of parsed commands
        parse_input(input, commands, &numCommands); // Parse the input

        // printf("\n\n***** DEBUGGING *****\n\n");
        // // Debugging: print the parsed commands
        // printf("Number of commands: %d\n\n", numCommands);
        // for (int i = 0; i < numCommands; i++)
        // {
        //     printf("Command %d: %s\n", i, commands[i].command);
        //     printf("Arguments: ");
        //     for (int j = 0; commands[i].args[j] != NULL; j++)
        //     {
        //         printf("%s ", commands[i].args[j]);
        //     }
        //     printf("\n");
        //     printf("Input file: %s\n", commands[i].inputFile);
        //     printf("Output file: %s\n\n", commands[i].outputFile);
        // }

        // Execute the parsed commands
        execute_commands(commands, &numCommands);

        // Reset the command structures and the counter for the next iteration
        reset_commands(commands, &numCommands);
    }

    // Prevent memory leaks by freeing allocated memory
    for (int i = 0; i < MAX_COMMANDS; i++)
    {
        free(commands[i].args);
    }

    return 0; // End of program
}
