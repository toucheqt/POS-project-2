/* ========================================================================== */
/*                                                                            */
/*   proj02.c                                                                 */
/*   (c) 2016 Ondrej Krpec, xkrpecqt@gmail.com                                */
/*                                                                            */
/*   Second project to subject Advanced operating systems at FIT VUT Brno.    */
/*   Project implements custom shell.                                         */
/*                                                                            */
/* ========================================================================== */

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define MAX_BUFFER_SIZE 513
#define MAX_LINE_SIZE 512

#define PROMPT "$ "
#define NEW_LINE_WITH_PROMPT "\n$ "
#define NEW_LINE_AS_STRING "\n"

#define TRAILING_ZERO '\0'
#define NEW_LINE '\n'

#define PIPE_INPUT '<'
#define PIPE_OUTPUT '>'
#define PIPE_BACKGROUND '&'

pthread_mutex_t mutex;
pthread_attr_t attr;
pthread_cond_t threadCondition = PTHREAD_COND_INITIALIZER;

pid_t childPid;

char buffer[MAX_BUFFER_SIZE];
 
bool isFinished = false;
bool isProcessRunning = false;
bool isInputTooLong = false; 
 
/**
 * Initializes sigaction structure.
 * @param sigset Sigaction structure.
 * @param signal Signal id.
 */
void initSigAction(struct sigaction sigset, int signal, void (*handler)(int));

/**
 * Prepares arguments for command from buffer.
 * @return Arguments standartized in argv array.
 */
char** prepareArguments();

/**
 * Simulates shell as it reads data from prompt and saves them into shared buffer. Therefore @see run can create the requested process.
 */
void* simulateShell();

/**
 * Parses command from buffer and runs it via system functions.
 */
void* run();

/**
 * Starts given command from buffer in a separate process.
 */
void startJob();

/**
 * Parses and removes filename from buffer. Input filename is parsed when constant PIPE_INPUT is used. Output filename is parsed when PIPE_OUTPUT is used.
 * @char pipe Possible values: PIPE_INPUT, PIPE_OUTPUT. Determines whether to parse input or output filename.
 * @return Filename.
 */
char* getFilename(char pipe);

/**
 * Functions parses input buffer and tries to find '&' which indicates running command in background.
 * @return True if buffer contains '&', false otherwise.
 */
bool isBackgroundJob();

/**
 * Function removes from input buffer redundant whitespaces.
 */
void trim();

/**
 * Functions valides input buffer;
 * @return True if buffer is valid, false otherwise.
 */
bool isBufferValid();

/**
 * Dumps output from command into file.
 * @param filename Name of the output file.
 */
void dump(char* filename);

/**
 * Sucks data for command from input file. =)
 * @param filename Name of the input file.
 */
void suck(char* filename);

/**
 * Cleans up garbage. Literally.
 */
void garbageCollector();
 
/**
 * Checks status code and if the status code is different than zero, cleans up and exists application.
 * @param status Status code.
 */
void threadError(int status); 

/**
 * Custom handler for standard signals.
 * @param sig Signal id.
 */
static void handler(int sig);

/** 
 * Custom handler for signals that come to emulated shell.
 * @param sig Signal id.
 */
static void killHandler(int sig);  

/**
 * Custom handler for signals that come to child process created from fork().
 * @param sig Signal id.
 */
static void childHandler(int sig);

/**
 * Ignores some specific signals, so background jobs wont be disturbed.
 */
void ignoreSignals();
 
/** Functions tests whether the buffer contains exit command.
 * @return True if the buffer contains exit command, false otherwise.
 */
bool isExit();

/**
 * Flushes text to stdout.
 * @param text Text that will be flushed.
 */
void flush(char* text);


int main() {

    struct sigaction sigset;
    
    pthread_t thread1Id;
    pthread_t thread2Id;
    
    void* status1;
    void* status2;
    
    initSigAction(sigset, SIGINT, handler);
    
    threadError(pthread_mutex_init(&mutex, NULL));
    threadError(pthread_attr_init(&attr));
    threadError(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE));
    threadError(pthread_create(&thread1Id, &attr, &simulateShell, NULL));
    threadError(pthread_create(&thread2Id, &attr, &run, NULL));
    threadError(pthread_join(thread1Id, &status1));
    threadError(pthread_join(thread2Id, &status2));
    
    garbageCollector();
    
    return EXIT_SUCCESS;
    
}


char** prepareArguments() {
    char **argv = (char**) malloc(sizeof(char*));
    char* argument = (char*) malloc(sizeof(char*));
    int i, j, argc;
    bool isLastSpace = false;
   
    trim();
    if (!isBufferValid()) {
        perror("Error: In prepareArguments(). Arguments are not valid.");
        return NULL;
    }
        
    for (i = 0, j = 1, argc = 0; i <= (int)strlen(buffer); i++) {   
        if (isspace(buffer[i])) {
            if (argc > 0) {
                argv = (char**) realloc(argv, sizeof(char*) * (argc + 1));
            }
            
            argv[argc] = argument;
            argument = (char*) malloc(sizeof(char*));
            argc++;
            j = 1; 
            isLastSpace = true;           
            continue;    
        }
        
        if (buffer[i] == PIPE_BACKGROUND) {
            isLastSpace = false;
            break;
        }
        
        if (!isgraph(buffer[i])) {
            continue;
        }
        
        argument[j - 1] = buffer[i];
        argument = (char*) realloc(argument, sizeof(char*) * (j + 1));
        j++;  
        
        isLastSpace = false;          
    }

   if (!isLastSpace && buffer[i] != PIPE_BACKGROUND) {
      argv[argc] = argument;
      argv = (char**) realloc(argv, sizeof(char*) * (argc + 1));
      argv[argc + 1] = NULL;
   } else {
      argv[argc] = NULL;
   }
   
	 return argv;
}


void initSigAction(struct sigaction sigset, int signal, void (*handler)(int)) {  
    memset(&sigset, 0, sizeof(sigset));
    sigset.sa_handler = handler;
    sigaction(signal, &sigset, NULL);
}


void* simulateShell() {
    while (!isFinished) {
        int bufferSize = 0;
        int tmpBufferSize = 0;
        
        isInputTooLong = false;
        memset(buffer, TRAILING_ZERO, MAX_BUFFER_SIZE);
    
        pthread_mutex_lock(&mutex);
        
        flush(PROMPT);
        
        while (true) {
            tmpBufferSize = read(fileno(stdin), &buffer, MAX_BUFFER_SIZE);
            bufferSize += tmpBufferSize;
            
            if (bufferSize > MAX_LINE_SIZE) {
                isInputTooLong = true;
            }
            
            if (buffer[tmpBufferSize - 1] == NEW_LINE) {
                break;
            }
        }
       
        if (!isInputTooLong) {
            buffer[bufferSize - 1] = TRAILING_ZERO;
        }
        
        isProcessRunning = true;
        pthread_cond_signal(&threadCondition);        

        if (isProcessRunning) {
            pthread_cond_wait(&threadCondition, &mutex);
        }  
        
        pthread_mutex_unlock(&mutex);
    }
    
    pthread_exit(NULL);
}


void* run() {   
    while (!isFinished) {
        pthread_mutex_lock(&mutex);
        if (!isProcessRunning) {
            pthread_cond_wait(&threadCondition, &mutex);
        }
        
        if (!isInputTooLong) {
            /* tests if the exit command is in buffer */
            if (!(buffer[0] == 'e' && buffer[1] == 'x' && buffer[2] == 'i' && buffer[3] == 't' && (buffer[4] == TRAILING_ZERO || isspace(buffer[4])))) {
                if (strlen(buffer) > 0) {
                    startJob();
                }              
            } else {
                isFinished = true;
            }
        } else {
            perror("Error: In run(). Input command exceeds 512 characters.");
        }
         
         isProcessRunning = false;
         
         pthread_cond_signal(&threadCondition);
         pthread_mutex_unlock(&mutex);
    }
    
    pthread_exit(NULL);
}


void startJob() {
    char* outputFilename = getFilename(PIPE_OUTPUT);
    char* inputFilename = getFilename(PIPE_INPUT);
    char **args = prepareArguments();                 
                
    if (args != NULL) {
        if ((childPid = fork()) == 0) {
            dump(outputFilename);
            suck(inputFilename);
                        
            if (isBackgroundJob()) {
                setsid();	
                ignoreSignals();
            }
                        
            if (execvp(args[0], args) == -1) {
                perror("Error: In run().");
                _exit(EXIT_FAILURE);
            }
                        
            _exit(EXIT_SUCCESS);
        } else if (childPid > 0) {
              struct sigaction sigset;
                    
              if (isBackgroundJob()) {
                  initSigAction(sigset, SIGCHLD, childHandler);
              } else {
                  int status;
                  initSigAction(sigset, SIGINT, killHandler);
                  sigset.sa_handler = killHandler;
                  if ((wait(&status)) == -1) {
                      perror("Error: In run(). Unexpected error in waiting for process to change state.");
                  }
              }
        } else if (childPid < 0) {
            perror("Error: In run(). Coulnt fork new process.");
        }
    }                   
} 


void garbageCollector() {
    pthread_attr_destroy(&attr);
	  pthread_cond_destroy(&threadCondition);	
	  pthread_mutex_destroy(&mutex);    
}


void threadError(int status) {
    if (status != 0) {
        garbageCollector();
        perror("Error: While working with threads.");
        exit(EXIT_FAILURE);
    }
}


static void handler(int sig) {
    if (SIGINT == sig) {
        flush(NEW_LINE_WITH_PROMPT);
    }       
}


static void killHandler(int sig) {
    if (SIGINT == sig) {
        if (kill(childPid, SIGINT) == 0) {
            flush(NEW_LINE_AS_STRING);
        } else {
            flush(NEW_LINE_WITH_PROMPT);
        }
    }
}


static void childHandler(int sig) {
    pid_t childPid;
    while ((childPid = waitpid(-1, NULL, WNOHANG)) > 0) {
        if (SIGCHLD == sig) {
            printf("Process (%d): Finished.", childPid);
            flush(NEW_LINE_WITH_PROMPT);
        }
    }
}


void ignoreSignals() {
    signal(SIGCHLD,SIG_DFL);
    signal(SIGTERM,SIG_DFL);
    signal(SIGINT,SIG_IGN); 
    signal(SIGTTIN,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);
    signal(SIGTTOU,SIG_IGN); 
    signal(SIGHUP, SIG_IGN);
}


bool isBackgroundJob() {
    int i;
    
    for (i = 0; i < MAX_BUFFER_SIZE; i++) {
        if (PIPE_BACKGROUND == buffer[i]) {
            return true;
        }
    }
    
    return false;
}


void trim() {
    bool isLastWhiteSpace = false;
    char* trimmedString = (char*) malloc(sizeof(char*));
    int i, j;
    
    for (i = 0, j = 0; i < (int)strlen(buffer); i++) {      
        if (isspace(buffer[i])) {
            if (isLastWhiteSpace) {
                continue;
            } else {
                isLastWhiteSpace = true;
                if (i == 0) {
                    continue;
                }
            }
        } else {
            isLastWhiteSpace = false;
        }
        
        trimmedString = (char*) realloc(trimmedString, sizeof(char*) * (j + 1));
        trimmedString[j] = buffer[i];
        j++;
    }
    
    memset(buffer, TRAILING_ZERO, MAX_BUFFER_SIZE);
    memcpy(buffer, trimmedString, strlen(trimmedString));
}


char* getFilename(char pipe) {
    char* filename = NULL;
    int index;
    int size;
    bool skipWhitespaces = true;
    
    if (PIPE_INPUT != pipe && PIPE_OUTPUT != pipe) {
        perror("Error: In getFilename(). Wrong input. Possible values are PIPE_INPUT or PIPE_OUTPUT.");
        garbageCollector();
        exit(EXIT_FAILURE);
    }
    
    for (index = 0; index < MAX_BUFFER_SIZE; index++) {
        if (buffer[index] == TRAILING_ZERO || buffer[index] == NEW_LINE) {
            return filename;
        }
        
        if (buffer[index] == pipe) {
            buffer[index] = ' ';
            index++;
            break;
        }
    }
    
    for (index = index, size = 0; index < MAX_BUFFER_SIZE; index++, size++) {
        if (buffer[index] == TRAILING_ZERO || buffer[index] == NEW_LINE) {
            break;
        }
        
        if (isspace(buffer[index])) {
            if (skipWhitespaces) {
                size--;
                continue;
            } else {
                break;
            }
        } else {
            if (skipWhitespaces) {
                skipWhitespaces = false;
                filename = (char*) malloc(sizeof(char*));
            } else {
                filename = (char*) realloc(filename, sizeof(char*) * (size + 1));
            }
            
            filename[size] = buffer[index];
            buffer[index] = ' ';
        }
    }
    
    if (filename != NULL) {    
        filename = (char*) realloc(filename, sizeof(char*) * (size + 1));
        filename[size + 1] = TRAILING_ZERO;
    }
    
    return filename;       
}


void dump(char* filename) {
  
    int fd;
    
    if (filename == NULL) {
        return;
    }

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd < 0) {
        perror("Error: In dump(). Cant open output file.");
    }
    
    close(STDOUT_FILENO);
    dup(fd);
    close (fd);
}


void suck(char* filename) {

    int fd;

    if (filename == NULL) {
        return;
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("Error: In suck(). Cant open input file.");
    }
    
    close(STDIN_FILENO);
    dup(fd);
    close(fd);
}


void flush(char* text) {
    printf(text);
    fflush(stdout);
}


bool isBufferValid() {
    if (strlen(buffer) == 0) {
        return false;
    }
    
    if (isspace(buffer[0])) {
        return false;
    }
    
    if (buffer[0] == PIPE_BACKGROUND) {
        return false;
    }
    
    return true;
}
