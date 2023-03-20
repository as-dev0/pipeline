# pipeline

I wrote this program to test my understanding of multithreading and the use of mutexes in C.

This program consists of four threads that interact with each other as producers and consumers. Mutexes are used to avoid race conditions.

The goal of the program is to take input from the user, which can be via the keyboard or from a file (using the < input redirection symbol),
and prints to stdout the user's input in lines of exactly 80 characters.
