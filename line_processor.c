#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>


/*

    A program with a pipeline of 4 threads that interact with each other as producers and consumers.

    - Input thread is the first thread in the pipeline. It gets input from the user and puts it in a 
    buffer it shares with the next thread in the pipeline.

    - Line separator thread is the second thread in the pipeline. It gets characters from the first buffer
    and replaces any new line characters with a space and puts the modified characters in the second buffer. 

    - Plus sign thread is the third thread in the pipeline. It gets characters from the second buffer 
    and replaces any ++ characters with a ^ and puts the modified characters in the third buffer.

    - Output thread is the fourth thread in the pipeline. It gets characters from the third buffer 
    and prints characters in batches of 80.

*/


// Takes one argument, a string, and sets it to an empty string
void empty(char* string){
  memset(string,0,strlen(string));
}


// Size of the buffers. For the purposes of this assignment, the buffers are unbounded.
#define SIZE 51000

// Number of lines
#define NUM_LINES 50

// stopWord is zero if the user hasn't entered \nSTOP\n, and one otherwise
int stopWord = 0;
// Number of lines to print including any adjustments from replacing ++ with ^
int linesToPrint = 0;
// Position of the S in \nSTOP\n. Initially set to zero when the user hasn't entered the STOP word.
int positionStop = 0;
// exit1 is zero if thread one hasn't exited, and one otherwise
int exit1 = 0;
// exit2 is zero if thread two hasn't exited, and one otherwise
int exit2 = 0;
// exit3 is zero if thread three hasn't exited, and one otherwise
int exit3 = 0;
// The number of ++ characters in the input
int numberPlus = 0;
// Final length of buffer_1 after all input has been received by the first thread
int finalLength1 = -1;
// Final length of buffer_2 after all input has been received by the second thread
int finalLength2 = -1;
// Final length of buffer_3 after all input has been received by the third thread
int finalLength3 = -1;


// Buffer 1, shared resource between input thread and line separator thread
char buffer_1[SIZE];
// Number of lines in the buffer
int count_1 = 0;
// Index where the line separator thread will pick up the next line
int con_idx_1 = 0;
// Initialize the mutex for buffer 1
pthread_mutex_t mutex_1 = PTHREAD_MUTEX_INITIALIZER;
// Initialize the condition variable for buffer 1
pthread_cond_t full_1 = PTHREAD_COND_INITIALIZER;

// Buffer 2, shared resource between line separator thread and plus sign thread
char buffer_2[SIZE];
// Number of lines in the buffer
int count_2 = 0;
// Index where the plus sign thread will pick up the next line
int con_idx_2 = 0;
// Initialize the mutex for buffer 2
pthread_mutex_t mutex_2 = PTHREAD_MUTEX_INITIALIZER;
// Initialize the condition variable for buffer 2
pthread_cond_t full_2 = PTHREAD_COND_INITIALIZER;

// Buffer 3, shared resource between plus sign thread and output thread
char buffer_3[SIZE];
// Number of lines in the buffer
int count_3 = 0;
// Index where the output thread will pick up the next line
int con_idx_3 = 0;
// Initialize the mutex for buffer 3
pthread_mutex_t mutex_3 = PTHREAD_MUTEX_INITIALIZER;
// Initialize the condition variable for buffer 3
pthread_cond_t full_3 = PTHREAD_COND_INITIALIZER;

// pOutput will be a concatenation of everything thread 4 gets from buffer_3
char pOutput[SIZE];
// Initialize the mutex for pOutput
pthread_mutex_t mutex_p = PTHREAD_MUTEX_INITIALIZER;
// Initialize the condition variable for pOutput
pthread_cond_t full_p = PTHREAD_COND_INITIALIZER;


// Takes one argument, a string named s, 
// and returns -1 if \nStop\n is not contained in s.
// Otherwise, returns the index of \nStop\n
int containsStop(char *s){

    for (int i = 0; i < strlen(s); i++){
        if (i < strlen(s)-4 & s[i]=='\n' & s[i+1] == 'S' & s[i+2] == 'T' & s[i+3] == 'O' & s[i+4] == 'P' & s[i+5]=='\n'){
            return i;
        }
    }
    return -1;
}


// Decrements positionStop by the number of ++ characters without double counting
// Increments numberPlus by the number of ++ characters without double counting
void countPlus(){
    
    for (int i = 0; i < strlen(buffer_1); i++){

        if (buffer_1[i]=='+' & buffer_1[i+1]=='+'){
            i++; // To avoid double counting
            numberPlus++;
            positionStop += -1;
            continue;
        }
    }
}


// -------------------------------Thread 1 Functions-------------------------------


/*
    Get input from the user.
    Takes one string argument and stores the next 1000 characters the user enters
    in the string argument.
*/
void get_user_input(char *line){
  read(STDIN_FILENO, line, 1000);
}


/*
    Takes one argument, a string named line, and puts it in buffer_1
*/
void put_buff_1(char *line){
  // Lock the mutex before putting the line in the buffer
  pthread_mutex_lock(&mutex_1);
  // Put the line in the buffer
  strcat(buffer_1,line);
  count_1++;
  // Signal to the consumer that the buffer is no longer empty
  pthread_cond_signal(&full_1);
  // Unlock the mutex
  pthread_mutex_unlock(&mutex_1);
}


/*
    Function that the input thread will run.
    Get input from the user.
    Put the input in buffer_1.
*/
void *get_input(void *args)
{
    empty(buffer_1);

    for (int i = 0; i < NUM_LINES; i++)
    {
        char item[1001]; empty(item);
        get_user_input(item);
        item[1001] = '\0';

        // Produce into buffer_1
        put_buff_1(item);


        // This if statement ends the for loop once the user
        // has entered \nSTOP\n
        if (containsStop(buffer_1) != -1){
            stopWord = 1;
            countPlus();
            finalLength1 = strlen(buffer_1);
            positionStop += containsStop(buffer_1)+1;
            linesToPrint += (positionStop+1)/80;
            exit1 = 1;
            pthread_cond_signal(&full_1); // This is done so that the line separator thread doesn't wait for more lines
            break;
        }
    }
    return NULL;
}


// -------------------------------Thread 2 Functions-------------------------------


/*
    Takes one argument, a string named line, and get the next line from buffer_1
    and stores it in line
*/
void get_buff_1(char *line){
    // Lock the mutex before checking if the buffer has data
    pthread_mutex_lock(&mutex_1);

    // When exit1 = 1, we don't want to wait for the input thread to put more data
    // in the buffer since it terminated, so we add it as a condition in the while
    // statement
    while (count_1 == 0 && exit1 == 0)
        // Buffer is empty. Wait for the producer to signal that the buffer has data
        pthread_cond_wait(&full_1, &mutex_1);

    // Gets characters from buffer_1 and stores it in line
    strncpy(line,buffer_1+con_idx_1,1000);
    line[1001] = '\0';

    con_idx_1 = con_idx_1 + strlen(line);
    count_1--;
    // Unlock the mutex
    pthread_mutex_unlock(&mutex_1);

}


/*
    Takes one argument, a string named line, and puts it in buffer_2
*/
void put_buff_2(char *line){
  // Lock the mutex before putting the line in the buffer
  pthread_mutex_lock(&mutex_2);
  // Put the line in the buffer
  strcat(buffer_2,line);
  count_2++;
  // Signal to the consumer that the buffer is no longer empty
  pthread_cond_signal(&full_2);
  // Unlock the mutex
  pthread_mutex_unlock(&mutex_2);
}


/*
    Takes two arguments, a string named line and a string named result,
    and replaces any newline character in line with a space. All non-newline
    characters remain unchanged, and the new characters are stored in result.
*/
void removeNewLines(char *line, char *result){

    for (int i = 0; i < strlen(line); i++){

        // Replace new lines with spaces
        if (line[i]=='\n'){
            strcat(result," ");
            continue;
        }

        // For non-new line characters, append to result
        char c[3]; empty(c);
        strncpy(c,line+i,1);
        c[2] = '\0';
        strcat(result,c);
    }
}


/*
    Function that the line_separator thread will run. 
    Consume a line from the buffer shared with the input thread.
    Replaces any newline characters with a space.
    Produce a line in the buffer shared with the plus sign thread.
*/
void *line_separator_thread(void *args)
{
    empty(buffer_2);
    for (int i = 0; i < NUM_LINES; i++)
    {
        // Consume from buffer_1
        char item[1001]; empty(item);
        get_buff_1(item);

        // Process
        char result[1001]; empty(result);
        removeNewLines(item,result);

        // Produce into buffer_2
        put_buff_2(result);

        // This if statement ends the for loop once the user
        // has entered \nSTOP\n
        if (strlen(buffer_2)==finalLength1){
            exit2 = 1;
            finalLength2 = strlen(buffer_2);
            pthread_cond_signal(&full_2); // This is done so that the plus sign thread doesn't wait for more lines
            break;
        }
    }
    return NULL;
}


// -------------------------------Thread 3 Functions-------------------------------


/*
    Takes one argument, a string named line, and get the next line from buffer_2
    and stores it in line
*/
void get_buff_2(char *line){
    // Lock the mutex before checking if the buffer has data
    pthread_mutex_lock(&mutex_2);

    // When exit2 = 1, we don't want to wait for the line separator thread to put more data
    // in the buffer since it terminated, so we add it as a condition in the while
    // statement
    while (count_2 == 0 && exit2 == 0)
        // Buffer is empty. Wait for the producer to signal that the buffer has data
        pthread_cond_wait(&full_2, &mutex_2);

    // Gets characters from buffer_2 and stores it in line
    strncpy(line,buffer_2+con_idx_2,1000);
    line[1001] = '\0';

    con_idx_2 = con_idx_2 + strlen(line);
    count_2--;
    // Unlock the mutex
    pthread_mutex_unlock(&mutex_2);

}


/*
    Takes one argument, a string named line, and puts it in buffer_3
*/
void put_buff_3(char *line){
  // Lock the mutex before putting the line in the buffer
  pthread_mutex_lock(&mutex_3);
  // Put the line in the buffer
  strcat(buffer_3,line);
  count_3++;
  // Signal to the consumer that the buffer is no longer empty
  pthread_cond_signal(&full_3);
  // Unlock the mutex
  pthread_mutex_unlock(&mutex_3);
}


/*
    Takes two arguments, a string named line and a string named result,
    and replaces any ++ characters in line with a ^. All other
    characters remain unchanged, and the new characters are stored in result.
*/
void removePlus(char *line, char *result){
    
    for (int i = 0; i < strlen(line); i++){

        // Replaces ++ with ^
        if (line[i]=='+' & line[i+1]=='+'){
            strcat(result,"^");
            i++;
            continue;
        }

        // For any non ++ characters, just append to result
        char c[3]; empty(c);
        strncpy(c,line+i,1);
        c[2] = '\0';
        strcat(result,c);
    }
}


/*
    Function that the plus sign thread will run. 
    Consume a line from the buffer shared with the line separator thread.
    Replaces ++ with ^
    Produce a line in the buffer shared with the output thread.

*/
void *plus_sign_thread(void *args)
{
    empty(buffer_3);
    for (int i = 0; i < NUM_LINES; i++)
    {
        // Consume from buffer_2
        char item[1001]; empty(item);
        get_buff_2(item);

        // Process
        char result[1001]; empty(result);
        removePlus(item,result);

        // Produce into buffer_3
        put_buff_3(result);

        // This if statement ends the for loop once the user
        // has entered \nSTOP\n
        if (strlen(buffer_3)==finalLength2-numberPlus){
            exit3 = 1;
            finalLength3 = strlen(buffer_3);
            pthread_cond_signal(&full_3); // This is done so that the output separator thread doesn't wait for more lines
            break;
        }
    }
    return NULL;
}


// -------------------------------Thread 4 Functions-------------------------------


/*
    Takes one argument, a string named line, and get the next line from buffer_3
    and stores it in line
*/
void get_buff_3(char *line){
    // Lock the mutex before checking if the buffer has data
    pthread_mutex_lock(&mutex_3);

    // When exit3 = 1, we don't want to wait for the input thread to put more data
    // in the buffer since it terminated, so we add it as a condition in the while
    // statement

    while (count_3 == 0 && exit3 == 0)
        // Buffer is empty. Wait for the producer to signal that the buffer has data
        pthread_cond_wait(&full_3, &mutex_3);

    // Gets characters from buffer_3 and stores it in line
    strncpy(line,buffer_3+con_idx_3,1000);
    line[1001] = '\0';

    con_idx_3 = con_idx_3 + strlen(line);
    count_3--;
    // Unlock the mutex
    pthread_mutex_unlock(&mutex_3);

}


/*
    Takes one argument, a string named line, and puts it in buffer_3
*/
void put_output(char *line){
  // Lock the mutex before putting the line in the buffer
  pthread_mutex_lock(&mutex_p);
  // Put the line in the buffer
  strcat(pOutput,line);
  // Unlock the mutex
  pthread_mutex_unlock(&mutex_p);
}


// This represents the number of lines already printed
int l = 0;


/*
    Takes one argument, a string named toPrint,
    and prints the string toPrint in lines of 80 characters.
    Takes into account the STOP word and does not print it or any character following it.
    Repeated applications of this function do not cause any lines to be duplicated because
    of the use of the global variable l, on which the for loop depends.
*/
void writer(char *toPrint){

    int p = strlen(toPrint)/80;
    
    // linesToPrint is the total number of lines that should be printed,
    // which is calculated using the position of \nSTOP\n
    if (linesToPrint != 0){
        p = linesToPrint;
    }

    for (int i = l; i < p ; i++){

        // If we received the \nSTOP\n, make sure not to print anything
        // after the \nSTOP\n
        if (stopWord != 0 &&  linesToPrint != 0 && l < linesToPrint   )   {
            write(STDOUT_FILENO, toPrint+i*80,80);
            write(STDOUT_FILENO, "\n",1);
            l++;
        // If we haven't yet received the \nSTOP\n, we don't need to care about the
        // total number of lines we should print, linesToPrint, because we could still
        // be getting input from the user
        } else if (stopWord == 0){
            write(STDOUT_FILENO, toPrint+i*80,80);
            write(STDOUT_FILENO, "\n",1);
            l++;
        }
    }
}


/*
    Function that the output thread will run. 
    Consume a line from the buffer shared with the plus sign thread.
    Print the line.
*/
void *write_output(void *args)
{
    empty(pOutput);
    int maxLinesOutput = (1001*50)/80;

    for (int i = 0; i < maxLinesOutput; i++)
    {
        // Consume from buffer_3
        char item[1001]; empty(item);
        get_buff_3(item);

        put_output(item);
        writer(pOutput);

        // This if statement ends the for loop once the user
        // has entered \nSTOP\n
        if (strlen(pOutput)==finalLength3){
            break;
        }
    }
    return NULL;
}


int main()
{
    srand(time(0));
    
    pthread_t input_t, line_separator_t, plus_sign_t, output_t;

    // Create the threads
    pthread_create(&input_t, NULL, get_input, NULL);
    pthread_create(&line_separator_t, NULL, line_separator_thread, NULL);
    pthread_create(&plus_sign_t, NULL, plus_sign_thread, NULL);
    pthread_create(&output_t, NULL, write_output, NULL);

    // Wait for the threads to terminate
    pthread_join(input_t, NULL);
    pthread_join(line_separator_t, NULL);
    pthread_join(plus_sign_t, NULL);
    pthread_join(output_t, NULL);

    return EXIT_SUCCESS;
}
