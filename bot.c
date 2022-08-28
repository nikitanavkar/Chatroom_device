#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "ioctl_bots_dev.h"

// Operation successful
#define SUCCESS 		1

// Operation result not known or no data 
#define NOT_EXPLORED 	0

// Operation failed
#define FAILURE 		-1

// Global file descriptor pointing to the chatroom character device
int fd;

// Global flag to exit the chatroom by exiting read and write threads
int quit = 0;

// Global pointer pointing to the current bot info
ProcessInfo *pinfo;

// Name of the bot
char *name;

/*
 * Function:  read_messages
 * -----------------------------------
 *  thread function to read messages from the chatroom
 * 
 *  *inp: Arguments to the thread (unused here)
 * 
 *  returns: NULL pointer to the join function.
 */
void * read_messages(void *inp) 
{
	ProcessInfo *l_msg;
	l_msg = (ProcessInfo *)malloc(sizeof(ProcessInfo));
	l_msg->id = pinfo->id;

	// Keep reading untill the bot leaves the chatroom
	while(!quit) 
	{
		// Clear the message buffer
		memset(l_msg->msg, 0, sizeof(l_msg->msg));

		// Read message from the chatroom
		int ret = ioctl(fd, RD_MESSAGE, l_msg);

		if(ret == FAILURE) 
		{
			printf("%s - %d read failed\n", name, pinfo->id);
			// Try again in next iteration
		}
		else if(ret > NOT_EXPLORED)
		{
			printf("%s\n", l_msg->msg);
		}
		else 
		{
			// No data in the messsage queue of the bot
		}
	}

	free(l_msg);
	return NULL;
}

/*
 * Function:  write_messages
 * -----------------------------------
 *  thread function to send messages to other bots in the chatroom
 * 
 *  *inp: Arguments to the thread (unused here)
 * 
 *  returns: NULL pointer to the join function.
 */
void * write_messages(void *inp) 
{
	ProcessInfo *l_msg;
	l_msg = (ProcessInfo *)malloc(sizeof(ProcessInfo));
	l_msg->id = pinfo->id;

	// Keep sending untill the bot leaves the chatroom
	while(!quit)
	{
		char buf[BUFF_LEN];

		// Clear the message buffer
		memset(l_msg->msg, 0, sizeof(l_msg->msg));

		// Add name of the  bot to the buffer
		memcpy(l_msg->msg, name, strlen(name));
		strcat(l_msg->msg, ": ");

		// Read message from the console
		printf("> ");
		fgets(buf, BUFF_LEN - strlen(name), stdin);
		
		// Check if the bot wants to leave the chatroom
		if(strcmp(buf, "Bye!\n") == 0)
		{
			strcat(l_msg->msg, "left");

			// Inform other bots in the chatroom that you've left the chat
			int ret = ioctl(fd, LEAVE_CHATROOM, l_msg);
			if(ret == FAILURE) 
			{
				printf("%s - %d leave chatroom failed\n", name, pinfo->id);
			}

			// Exit the chat and inform this to reading thread as well
			quit = 1;
			break;
		}

		// Add message to the structure that is sent to the chatroom
		strcat(l_msg->msg, buf);

		// Send message to the other bots in the chatroom
		int ret = ioctl(fd, WR_MESSAGE, l_msg);
		if(ret == FAILURE)
		{
			printf("%s - %d write failed\n", name, pinfo->id);
			// Write failed try again in the next iteration
		}
	}

	free(l_msg);
	return NULL;
}

/*
 * Function:  main
 * -----------------------------------
 *  main function to start the bot and try to join the chatroom
 * 
 *  argc: Number of command line arguments 
 * 
 *  *argv[]: Array of strings storing the command line arguments
 * 
 *  returns: Exit code to indicate type of failure, 0 - Successfull exit
 */
int main(int argc, char const *argv[]) 
{
	// name of the bot is required
	if (argc < 2)
    {
        printf("Chatbot Help: Usage %s bot_name\n", argv[0]);
        exit(1);
    }

	pinfo = (ProcessInfo *)malloc(sizeof(ProcessInfo));
	pinfo->id = getpid();
	name = (char *)malloc(strlen(argv[1]));
	//sprintf(name, argv[1]);
	 strcpy(name, argv[1]);

	// Open the chatroom
	fd = open("/dev/akash_chatroom", O_RDWR);
	if( fd == FAILURE) 
	{
		printf("Open failed for %s - %d\n", name, pinfo->id);
		free(pinfo);
		free(name);
		exit(2);
	}

	// Add name of the  bot to the buffer
	memcpy(pinfo->msg, name, strlen(name));
	strcat(pinfo->msg, ": joined");

	// Failed to join the chatroom
	if(ioctl(fd, JOIN_CHATROOM, pinfo) == -1)
	{
		printf("Joining chatroom failed for %s - %d\n", name, pinfo->id);
		close(fd);
		free(pinfo);
		free(name);
		exit(3);
	}
	else
	{
		printf("You joined as %s with pid = %d\n", name, pinfo->id);
		pthread_t read_t, write_t;

		// Create thread to read messages from chatroom
		pthread_create(&read_t, NULL, read_messages, NULL);

		// Create thread to send messages to other bots in the chatroom
		pthread_create(&write_t, NULL, write_messages, NULL);

		// Wait for the threads to exit
		pthread_join(read_t, NULL);
		pthread_join(write_t, NULL);
	}
	printf("%s - %d leaving  the chat\n", name, pinfo->id);
	close(fd);
	free(pinfo);
	free(name);
	exit(0);
}