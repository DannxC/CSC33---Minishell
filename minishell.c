#include <stdio.h>     // For input/output functions
#include <stdlib.h>    // For general utilities like exit()
#include <string.h>    // For string manipulation functions
#include <unistd.h>    // For POSIX operating system API, e.g., fork()
#include <sys/types.h> // For system data type definitions
#include <sys/wait.h>  // For waiting for process termination
#include <fcntl.h>     // For file control options

#define MAX_LINE 1024 // Define a constant for maximum input line length
#define MAX_ARGS 32   // Maximum number of arguments

// Function to parse user input into command, arguments, and file redirections
void parse_input(char *input, char **command, char ***(args), char **inputFile, char **outputFile)
{
    // Initialize pointers to NULL at the start
    *inputFile = NULL;
    *outputFile = NULL;
    int argCount = 0; // Keep track of the number of arguments

    // Tokenize the input string using space as a delimiter
    char *token = strtok(input, " ");
    *command = token; // The first token is the command
    (*args)[argCount++] = token; // The first token is also the first argument

    // Iterate over the rest of the tokens to parse arguments and redirections
    while (token != NULL)
    {
        token = strtok(NULL, " "); // Move to the next token
        if (token == NULL)
            break; // Skip NULL tokens

        if (strcmp(token, "<") == 0)
        { // If token is input redirection symbol
            token = strtok(NULL, " ");
            *inputFile = token; // Next token is the input file name
        }
        else if (strcmp(token, ">") == 0)
        { // If token is output redirection symbol
            token = strtok(NULL, " ");
            *outputFile = token; // Next token is the output file name
        }
        else
        {
            // If it's not a redirection, consider it as part of the arguments
            if (argCount < MAX_ARGS - 1)
            { // Leave space for NULL terminator
                (*args)[argCount++] = token;
            }
        }
    }
    (*args)[argCount] = NULL; // NULL-terminate the arguments array
}

void execute_command(char *command, char *args[], char *inputFile, char *outputFile)
{
    pid_t pid = fork();

    if (pid == 0)
    { // Child process


        /* Handle input/output redirection */

        if (inputFile != NULL)
        {
            FILE *fpIn = fopen(inputFile, "r"); // Open file for reading
            if (fpIn == NULL)
            {
                exit(EXIT_FAILURE); // Exit the child process if file opening fails
            }
            dup2(fileno(fpIn), STDIN_FILENO); // Duplicate file descriptor to stdin (file descriptor = 0)
            //fclose(fpIn);                     // Close the file stream
        }

        if (outputFile != NULL)
        {
            FILE *fpOut = fopen(outputFile, "w"); // Open file for writing (this creates the file if it doesn't exist)
            if (fpOut == NULL)
            {
                exit(EXIT_FAILURE); // Exit the child process if file creation fails
            }
            dup2(fileno(fpOut), STDOUT_FILENO); // Duplicate file descriptor to stdout (file descriptor = 1)
            //fclose(fpOut);                      // Close the file stream
        }

        /* Executing the command */
        // char *pathname = command;
        // char *argv[] = {pathname, args, NULL};
        char *envp[] = {NULL};
        execve(command, args, envp); // Replace the current process image with a new one
    }
    else if (pid > 0)
    {               // Parent process
        wait(NULL); // Wait for the child process to finish
    }
    else
    {
        perror("fork");
    }
}

int main()
{
    char input[MAX_LINE]; // Buffer for the user input
    char *command;
    char **args = malloc(MAX_ARGS * sizeof(char*)); // Array of arguments
    char *inputFile;
    char *outputFile;

    // Main loop to continuously accept user commands
    while (1)
    {
        printf("\033[0;32mcmd> \033[0m"); // Prompt the user for input

        // Read a line of input and exit the loop if EOF is encountered
        if (!fgets(input, MAX_LINE, stdin))
        {
            break;
        }

        // Remove the trailing newline character from the input and substitute it
        // with a null character, truncating the string at this point
        input[strcspn(input, "\n")] = '\0';

        // Parse the input and execute the command
        parse_input(input, &command, &args, &inputFile, &outputFile);
        execute_command(command, args, inputFile, outputFile);
    }

    return 0; // End of program
}
